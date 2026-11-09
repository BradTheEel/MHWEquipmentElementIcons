param([Parameter(Mandatory = $true)][string]$ZigPath)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-common.ps1')

$output = Join-Path $PSScriptRoot 'build/EquipmentElementIcons.dll'
$plugin = Join-Path $PSScriptRoot 'src/plugin.c'
Invoke-EquipmentElementBuild -ZigPath $ZigPath -Sources $plugin -CompilerFlags '-shared' -OutputPath $output
Write-Output $output
