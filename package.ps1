<#
.SYNOPSIS
    Builds the CMake targets and assembles the runnable distribution folder.

.DESCRIPTION
    The package entry point is ArkFrameworkLoader.exe. Double-click it, launch Arknights,
    and the loader waits for the new game process, injects ArkFramework.dll, and starts
    BetterBattleWeb automatically.

    Only damage_meter, BetterBattle, CustomSkills and web_bridge are included. AutoLogin and BattleCheat
    are intentionally excluded.

.PARAMETER OutputDirectory
    Package root inside the repository. It is deleted and recreated.

.PARAMETER BuildDirectory
    CMake build directory. Defaults to build-package so packaging never overwrites
    DLLs currently mapped by a running game.

.PARAMETER SkipBuild
    Reuses the existing CMake outputs and only assembles the package.

.PARAMETER PatchFrom
    Optional previous package root (the folder containing ArkFrameworkLoader.exe).
    When set, also emits an ArkFramework-patch-*.zip containing only changed,
    added or deleted files relative to that baseline.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory = 'package',
    [string]$BuildDirectory = 'build-package',
    [switch]$SkipBuild,
    [string]$PatchFrom = ''
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
        'CustomSkills',
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
    'plugins\CustomSkills.dll' = Join-Path $Dist 'plugins\CustomSkills.dll'
    'plugins\web_bridge.dll'   = Join-Path $Dist 'plugins\web_bridge.dll'
    'custom_complex_arc.json'  = Join-Path $Dist 'custom_complex_arc.json'
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
$ExpectedPlugins = @('BetterBattle.dll', 'CustomSkills.dll', 'damage_meter.dll', 'web_bridge.dll')
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

if ($PatchFrom) {
    $Baseline = [IO.Path]::GetFullPath(
        $(if ([IO.Path]::IsPathRooted($PatchFrom)) {
            $PatchFrom
        } else {
            Join-Path $Root $PatchFrom
        }))
    if (-not (Test-Path -LiteralPath (Join-Path $Baseline 'ArkFrameworkLoader.exe'))) {
        throw "Patch baseline is not a package root: $Baseline"
    }
    if ($Baseline.StartsWith($Output.TrimEnd('\') + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The patch baseline must be outside the output directory.'
    }

    $PatchRoot = Join-Path $Output 'ArkFramework-patch'
    if (Test-Path -LiteralPath $PatchRoot) {
        Remove-Item -LiteralPath $PatchRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $PatchRoot -Force | Out-Null

    $Changed = [Collections.Generic.List[string]]::new()
    $Deleted = [Collections.Generic.List[string]]::new()
    $IgnoredPaths = @(
        'cache\*',
        'logs\*',
        'exports\*',
        'recordings\*',
        'rogue_captures\*',
        'BetterBattleWeb\art_cache\*',
        'BetterBattle.request',
        'BetterBattle.status',
        'CustomSkills.request',
        'CustomSkills.status',
        'auto_login.enabled',
        'auto_login.status',
        'battle_probe.jsonl',
        'battle_trace_frames.log',
        'battle_operations.json'
    )
    function Test-IgnoredPatchPath([string]$relative) {
        $normalized = $relative.Replace('/', '\')
        foreach ($pattern in $IgnoredPaths) {
            if ($normalized -like $pattern) {
                return $true
            }
        }
        return $false
    }
    foreach ($file in Get-ChildItem -LiteralPath $Release -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($Release, $file.FullName)
        if (Test-IgnoredPatchPath $relative) { continue }
        $old = Join-Path $Baseline $relative
        $same = $false
        if (Test-Path -LiteralPath $old) {
            $same =
                (Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash -eq
                (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
        if ($same) { continue }
        $Changed.Add($relative)
        $destination = Join-Path $PatchRoot $relative
        $directory = Split-Path -Parent $destination
        if ($directory) {
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    foreach ($file in Get-ChildItem -LiteralPath $Baseline -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($Baseline, $file.FullName)
        if (Test-IgnoredPatchPath $relative) { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $Release $relative))) {
            $Deleted.Add($relative)
        }
    }

    $Manifest = [ordered]@{
        format = 1
        createdAt = (Get-Date).ToString('o')
        baseline = $Baseline
        files = @($Changed)
        deleted = @($Deleted)
    }
    $Manifest |
        ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $PatchRoot 'patch-manifest.json') `
            -Encoding utf8

    $ApplyPatch = @'
param([Parameter(Mandatory = $true)][string]$Target)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Get-Content -LiteralPath (Join-Path $root 'patch-manifest.json') `
    -Raw | ConvertFrom-Json
$targetRoot = [IO.Path]::GetFullPath($Target).TrimEnd('\')
foreach ($relative in @($manifest.files)) {
    $destination = [IO.Path]::GetFullPath(
        (Join-Path $targetRoot $relative))
    if (-not $destination.StartsWith($targetRoot + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe patch path: $relative"
    }
    $directory = Split-Path -Parent $destination
    if ($directory) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $root $relative) `
        -Destination $destination -Force
}
foreach ($relative in @($manifest.deleted)) {
    $destination = [IO.Path]::GetFullPath(
        (Join-Path $targetRoot $relative))
    if ($destination.StartsWith($targetRoot + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
    }
}
Write-Output ("Applied {0} file(s), removed {1} file(s) to {2}" -f
    @($manifest.files).Count, @($manifest.deleted).Count, $targetRoot)
'@
    Set-Content -LiteralPath (Join-Path $PatchRoot 'apply-patch.ps1') `
        -Value $ApplyPatch -Encoding utf8

    $PatchReadme = @'
ArkFramework 增量补丁使用说明

1. 先完全关闭 Arknights、ArkFrameworkLoader 和 BetterBattleWeb。
2. 将本压缩包解压到任意临时目录。
3. 打开 PowerShell，进入解压后的目录。
4. 执行：

   .\apply-patch.ps1 -Target "D:\path\to\ArkFramework"

   -Target 指向旧版完整包中的 ArkFramework 目录，该目录必须包含
   ArkFrameworkLoader.exe。
5. 脚本会读取 patch-manifest.json，覆盖变更文件并删除废弃文件。
6. 完成后重新运行 ArkFrameworkLoader.exe，再启动游戏。

注意：

- 不要直接解压到游戏安装目录。
- 不要手工只覆盖部分文件，请运行 apply-patch.ps1。
- 建议先备份旧版 ArkFramework 目录。
- 如果 PowerShell 阻止运行脚本，可临时使用：

  powershell -ExecutionPolicy Bypass -File .\apply-patch.ps1 -Target "D:\path\to\ArkFramework"

- 补丁只包含相对上一版发生变化的文件；不要把它当作完整安装包。

如果这是诊断补丁，复现崩溃后请把以下文件发给开发者：

- %LOCALAPPDATA%\ArkFramework\native-crash.log
- 发布包目录\logs\ArkFramework.log
- %LOCALAPPDATA%\BetterBattleWeb\host.log
'@
    Set-Content -LiteralPath (Join-Path $PatchRoot 'README.txt') `
        -Value $PatchReadme -Encoding utf8

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $ZipPath = Join-Path $Output (
        'ArkFramework-patch-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
    [IO.Compression.ZipFile]::CreateFromDirectory($PatchRoot, $ZipPath)
    $PatchTotal = Get-ChildItem -LiteralPath $PatchRoot -Recurse -File |
        Measure-Object Length -Sum
    Write-Output ("Patch ready: {0} (changed={1} deleted={2} files={3} SizeMB={4})" -f
        $ZipPath, $Changed.Count, $Deleted.Count, $PatchTotal.Count,
        [math]::Round((Get-Item -LiteralPath $ZipPath).Length / 1MB, 2))
    Remove-Item -LiteralPath $PatchRoot -Recurse -Force
}
