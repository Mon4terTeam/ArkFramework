<#
.SYNOPSIS
    Builds a release and assembles the self-contained distribution folder.

.DESCRIPTION
    Runs build.ps1 -Target All -Release (including the automated tests unless -SkipTests is
    given) and copies the loader, core, shared ImGui, bootstrap, plugins and licenses into
    <OutputDirectory>\ArkFramework. Nothing is installed into the game directory.

.PARAMETER OutputDirectory
    Package root inside the repository. It is deleted and recreated.

.PARAMETER SkipTests
    Builds the release without building or running the automated tests.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory = 'package',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$Output = [IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (-not $Output.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The output directory must stay inside the repository.'
}

$Dist = Join-Path $Root 'build\release\dist'
& (Join-Path $Root 'build.ps1') -Target All -Release -DistDirectory $Dist -SkipTests:$SkipTests

$Release = Join-Path $Output 'ArkFramework'
if (Test-Path -LiteralPath $Output) {
    Remove-Item -LiteralPath $Output -Recurse -Force
}
foreach ($directory in @($Release, (Join-Path $Release 'plugins'), (Join-Path $Release 'licenses'))) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$Files = [ordered]@{
    'ArkFrameworkLoader.exe'   = Join-Path $Dist 'ArkFrameworkLoader.exe'
    'ArkFramework.dll'         = Join-Path $Dist 'ArkFramework.dll'
    'ArkImGui.dll'             = Join-Path $Dist 'ArkImGui.dll'
    'ArkBootstrap.dll'         = Join-Path $Dist 'ArkBootstrap.dll'
    'plugins\damage_meter.dll' = Join-Path $Dist 'plugins\damage_meter.dll'
    'LICENSE'                  = Join-Path $Root 'LICENSE'
    'licenses\Dear-ImGui.txt'  = Join-Path $Root 'third_party\imgui-1.91.9b\LICENSE.txt'
    'licenses\MinHook.txt'     = Join-Path $Root 'third_party\minhook-1.3.4\LICENSE.txt'
}
foreach ($entry in $Files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        throw "Missing package input: $($entry.Value)"
    }
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $Release $entry.Key)
}

Write-Output "Release ready: $Release"
Get-ChildItem -LiteralPath $Release -Recurse -File |
    Select-Object @{ Name = 'Path'; Expression = { $_.FullName.Substring($Release.Length + 1) } }, Length |
    Format-Table -AutoSize
