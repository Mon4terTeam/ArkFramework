<#
.SYNOPSIS
    Builds the CMake targets and assembles the runnable distribution folder.

.DESCRIPTION
    The package entry point is ArkFrameworkLoader.exe. Double-click it, launch Arknights,
    and the loader waits for the new game process, injects ArkFramework.dll, and starts
    BetterBattleWeb automatically.

    Only damage_meter, BetterBattle and web_bridge are included. AutoLogin and BattleCheat
    are intentionally excluded.

.PARAMETER OutputDirectory
    Package root inside the repository. It is deleted and recreated.

.PARAMETER BuildDirectory
    CMake build directory. Defaults to build-package so packaging never overwrites
    DLLs currently mapped by a running game.

.PARAMETER SkipBuild
    Reuses the existing CMake outputs and only assembles the package.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory = 'package',
    [string]$BuildDirectory = 'build-package',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$Output = [IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (-not $Output.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The output directory must stay inside the repository.'
}

$Build = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $Root $BuildDirectory
}))
if (-not $Build.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The build directory must stay inside the repository.'
}
$Dist = Join-Path $Build 'dist'

if (-not $SkipBuild) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake was not found on PATH.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Build 'CMakeCache.txt'))) {
        & cmake -S $Root -B $Build -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed with exit code $LASTEXITCODE."
        }
    }

    $Targets = @(
        'ArkFramework',
        'ArkFrameworkLoader',
        'ArkBootstrap',
        'ArkImGui',
        'damage_meter',
        'BetterBattle',
        'web_bridge',
        'BetterBattleWeb'
    )
    foreach ($target in $Targets) {
        Write-Host "Building $target..."
        & cmake --build $Build --config Release --target $target -j 8
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for target $target."
        }
    }
}

$Release = Join-Path $Output 'ArkFramework'
if (Test-Path -LiteralPath $Output) {
    $InUse = Get-Process Arknights, ArkFrameworkLoader, BetterBattleWeb `
        -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Path -and $_.Path.StartsWith($Release, [StringComparison]::OrdinalIgnoreCase)
        }
    if ($InUse) {
        throw ("Close the running package before rebuilding: " +
            (($InUse | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ', '))
    }
    Remove-Item -LiteralPath $Output -Recurse -Force
}
foreach ($directory in @(
    $Release,
    (Join-Path $Release 'plugins'),
    (Join-Path $Release 'licenses')
)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$Gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $Gcc) {
    throw 'gcc was not found on PATH; MinGW runtime DLLs cannot be packaged.'
}
$MingwBin = Split-Path -Parent $Gcc.Source
$ApiMsCrt = Join-Path $Dist 'api-ms-win-crt-private-l1-1-0.dll'
if (-not (Test-Path -LiteralPath $ApiMsCrt)) {
    $ApiMsCrt = Join-Path $Root 'build-cmake\dist\api-ms-win-crt-private-l1-1-0.dll'
}

$Files = [ordered]@{
    'ArkFrameworkLoader.exe'   = Join-Path $Dist 'ArkFrameworkLoader.exe'
    'ArkFramework.dll'         = Join-Path $Dist 'ArkFramework.dll'
    'ArkImGui.dll'             = Join-Path $Dist 'ArkImGui.dll'
    'ArkBootstrap.dll'         = Join-Path $Dist 'ArkBootstrap.dll'
    'libgcc_s_seh-1.dll'       = Join-Path $MingwBin 'libgcc_s_seh-1.dll'
    'libstdc++-6.dll'          = Join-Path $MingwBin 'libstdc++-6.dll'
    'libwinpthread-1.dll'      = Join-Path $MingwBin 'libwinpthread-1.dll'
    'api-ms-win-crt-private-l1-1-0.dll' = $ApiMsCrt
    'plugins\damage_meter.dll' = Join-Path $Dist 'plugins\damage_meter.dll'
    'plugins\BetterBattle.dll' = Join-Path $Dist 'plugins\BetterBattle.dll'
    'plugins\web_bridge.dll'   = Join-Path $Dist 'plugins\web_bridge.dll'
    'LICENSE'                  = Join-Path $Root 'LICENSE'
    'README.md'                = Join-Path $Root 'README.md'
    'licenses\Dear-ImGui.txt'  = Join-Path $Root 'third_party\imgui-1.91.9b\LICENSE.txt'
    'licenses\MinHook.txt'     = Join-Path $Root 'third_party\minhook-1.3.4\LICENSE.txt'
}
foreach ($entry in $Files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        throw "Missing package input: $($entry.Value)"
    }
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $Release $entry.Key)
}

$WebSource = Join-Path $Dist 'BetterBattleWeb'
if (-not (Test-Path -LiteralPath (Join-Path $WebSource 'BetterBattleWeb.exe'))) {
    throw "Missing BetterBattleWeb publish output: $WebSource"
}
Copy-Item -LiteralPath $WebSource -Destination (Join-Path $Release 'BetterBattleWeb') -Recurse

$Forbidden = Get-ChildItem -LiteralPath $Release -Recurse -File |
    Where-Object {
        $_.Name -in @('auto_login.dll', 'battle_cheat.dll', 'BattleCheat.dll')
    }
if ($Forbidden) {
    throw "Forbidden plugin found in package: $($Forbidden.FullName -join ', ')"
}

$PluginNames = Get-ChildItem -LiteralPath (Join-Path $Release 'plugins') -File |
    Select-Object -ExpandProperty Name |
    Sort-Object
$ExpectedPlugins = @('BetterBattle.dll', 'damage_meter.dll', 'web_bridge.dll')
if (Compare-Object $PluginNames $ExpectedPlugins) {
    throw "Unexpected plugin set: $($PluginNames -join ', ')"
}

Write-Output "Release ready: $Release"
Write-Output 'Run ArkFrameworkLoader.exe, then launch Arknights.'
$Total = Get-ChildItem -LiteralPath $Release -Recurse -File |
    Measure-Object Length -Sum
Write-Output ("Files={0} SizeMB={1}" -f $Total.Count,
    [math]::Round($Total.Sum / 1MB, 1))
Get-ChildItem -LiteralPath $Release -File |
    Select-Object Name, Length |
    Format-Table -AutoSize
Get-ChildItem -LiteralPath (Join-Path $Release 'plugins') -File |
    Select-Object Name, Length |
    Format-Table -AutoSize
