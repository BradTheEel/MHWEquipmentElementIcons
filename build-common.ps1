function Invoke-EquipmentElementBuild {
    param(
        [Parameter(Mandatory = $true)][string]$ZigPath,
        [Parameter(Mandatory = $true)][string[]]$Sources,
        [Parameter(Mandatory = $true)][string[]]$CompilerFlags,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    $ZigPath = (Resolve-Path -LiteralPath $ZigPath).Path
    $version = & $ZigPath version
    if ('0.13.0' -ne $version -or $LASTEXITCODE -ne 0) {
        throw 'Zig 0.13.0 is required.'
    }

    $build = Split-Path -Parent $OutputPath
    $src = Join-Path $PSScriptRoot 'src'
    $vendor = Join-Path $src 'vendor'
    $atlasIncludes = Join-Path $build ((Split-Path -Leaf $OutputPath) + '.includes')
    New-Item -ItemType Directory -Path $atlasIncludes -Force | Out-Null

    $layout = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'atlas-layout.json') | ConvertFrom-Json
    $resource = [string]$layout.resource_path
    foreach ($value in $layout.columns, $layout.rows, $layout.tile_size) {
        if ($value -isnot [int] -and $value -isnot [long]) {
            throw 'Atlas columns, rows and tile_size must be integers.'
        }
    }
    $columns = [int]$layout.columns
    $rows = [int]$layout.rows
    $tileSize = [int]$layout.tile_size
    $effects = @($layout.icons).Count
    if ($resource -cnotmatch '\Aui(?:/[a-zA-Z0-9_]+)+\z' -or $resource.Length -gt 79 -or
        $columns -lt 1 -or $rows -lt 1 -or $tileSize -ne 64 -or
        $columns * $tileSize -gt 32767 -or $rows * $tileSize -gt 32767 -or
        -not $layout.icons -or $effects -gt 256 -or $effects -gt $columns * $rows) {
        throw 'Invalid atlas-layout.json.'
    }
    for ($slot = 0; $slot -lt $effects; ++$slot) {
        if ($layout.icons[$slot].id -ne ($slot + 1)) {
            throw 'Atlas icons must be ordered by effect ID starting at 1.'
        }
    }
    $resourceLiteral = ConvertTo-Json -InputObject $resource.Replace('/', '\') -Compress
    $atlasHeader = @"
#ifndef EQUIPMENT_ELEMENT_ATLAS_LAYOUT_H
#define EQUIPMENT_ELEMENT_ATLAS_LAYOUT_H

#define ATLAS_RESOURCE_PATH $resourceLiteral
enum {
    ATLAS_TILE_SIZE = $tileSize,
    ATLAS_COLUMNS = $columns,
    ATLAS_ROWS = $rows,
    ATLAS_WIDTH = ATLAS_TILE_SIZE * ATLAS_COLUMNS,
    ATLAS_HEIGHT = ATLAS_TILE_SIZE * ATLAS_ROWS,
    ATLAS_EFFECT_COUNT = $effects
};

#endif
"@
    [System.IO.File]::WriteAllText((Join-Path $atlasIncludes 'atlas_layout.h'), $atlasHeader)

    $arguments = @(
        'cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall', '-Wextra', '-fno-strict-aliasing',
        '-I', $src, '-I', $atlasIncludes, '-I', (Join-Path $vendor 'include')
    ) + $CompilerFlags + $Sources + @(
        (Join-Path $src 'badge.c'),
        (Join-Path $vendor 'src/hook.c'), (Join-Path $vendor 'src/trampoline.c'),
        (Join-Path $vendor 'src/buffer.c'), (Join-Path $vendor 'src/hde/hde64.c'),
        '-lkernel32', '-o', $OutputPath
    )

    $priorGlobalCache = $env:ZIG_GLOBAL_CACHE_DIR
    $priorLocalCache = $env:ZIG_LOCAL_CACHE_DIR
    if (-not $priorGlobalCache) { $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build '.zig-global-cache' }
    if (-not $priorLocalCache) { $env:ZIG_LOCAL_CACHE_DIR = Join-Path $build '.zig-local-cache' }
    try {
        & $ZigPath @arguments
        if ($LASTEXITCODE -ne 0) { throw "Zig failed with exit code $LASTEXITCODE." }
    } finally {
        $env:ZIG_GLOBAL_CACHE_DIR = $priorGlobalCache
        $env:ZIG_LOCAL_CACHE_DIR = $priorLocalCache
    }
}
