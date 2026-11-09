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
    New-Item -ItemType Directory -Path $build -Force | Out-Null

    $arguments = @(
        'cc', '-target', 'x86_64-windows-gnu', '-O2', '-Wall', '-Wextra', '-fno-strict-aliasing',
        '-I', $src, '-I', (Join-Path $vendor 'include')
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
