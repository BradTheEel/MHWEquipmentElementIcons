param(
    [Parameter(Mandatory = $true)][string]$ZigPath,
    [Parameter(Mandatory = $true)][string]$LauncherPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-common.ps1')

$LauncherPath = (Resolve-Path -LiteralPath $LauncherPath).Path
$output = Join-Path $PSScriptRoot 'build/box_test.exe'
$tests = @(
    (Join-Path $PSScriptRoot 'tests/box_test.c'),
    (Join-Path $PSScriptRoot 'tests/badge_test.c'),
    (Join-Path $PSScriptRoot 'tests/sprite_fixture.c')
)
Invoke-EquipmentElementBuild -ZigPath $ZigPath -Sources $tests -CompilerFlags '-Werror' -OutputPath $output
& $LauncherPath $output
if ($LASTEXITCODE -ne 0) { throw "Fixture failed with exit code $LASTEXITCODE." }
