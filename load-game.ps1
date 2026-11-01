<#
.SYNOPSIS
Loads ArkFramework into Arknights with dist\ArkFrameworkLoader.exe.

.DESCRIPTION
Selects the game process (a running Arknights.exe, an explicit PID, or with -Watch the next
newly started one), runs the native loader and prints its JSON result. Each run writes its
report to .tools\runtime-load-<pid|watch>-<guid>.json.

.PARAMETER GameProcessId
PID of a running Arknights.exe. When omitted (and -Watch is not used), exactly one running
Arknights process must exist.

.PARAMETER Watch
Arm the loader before the game starts; it selects the next new Arknights.exe by name.
Incompatible with -GameProcessId.

.PARAMETER TimeoutSeconds
Overall loader deadline, 1-600 seconds.

.PARAMETER Method
Loading method: window-hook or remote-thread. Defaults to the loader's choice.

.PARAMETER ElevateIfRequired
Run the loader elevated (one UAC prompt) when this shell is not elevated.

.EXAMPLE
.\load-game.ps1 -Watch -ElevateIfRequired
#>
param(
    [uint32]$GameProcessId = 0,
    [switch]$Watch,
    [ValidateRange(1, 600)][int]$TimeoutSeconds = 60,
    [ValidateSet('window-hook', 'remote-thread')][string]$Method,
    [switch]$ElevateIfRequired
)
$ErrorActionPreference = 'Stop'

if ($Watch -and $GameProcessId) {
    throw '-Watch and -GameProcessId are incompatible.'
}
$loaderPath = Join-Path $PSScriptRoot 'dist\ArkFrameworkLoader.exe'
if (-not (Test-Path -LiteralPath $loaderPath)) {
    throw 'Build ArkFrameworkLoader.exe first.'
}

$loaderArguments = @()
if ($Watch) {
    # No existing process is opened or loaded. The native watcher records the baseline and
    # arming time itself, then selects a new Arknights.exe by name.
    $loaderArguments += @('--watch')
    $reportLabel = 'watch'
} else {
    if ($GameProcessId) {
        $gameProcess = Get-Process -Id $GameProcessId
        if ($gameProcess.ProcessName -ne 'Arknights') {
            throw 'The selected process is not Arknights.'
        }
    } else {
        $gameProcesses = @(Get-Process -Name Arknights -ErrorAction SilentlyContinue)
        if ($gameProcesses.Count -ne 1) {
            throw 'Specify -GameProcessId for a running game, or use -Watch before starting it yourself.'
        }
        $gameProcess = $gameProcesses[0]
    }
    $loaderArguments += @('--pid', [string]$gameProcess.Id)
    $reportLabel = [string]$gameProcess.Id
}

$reportDirectory = Join-Path $PSScriptRoot '.tools'
[System.IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
$reportName = 'runtime-load-' + $reportLabel + '-' + [guid]::NewGuid().ToString('N') + '.json'
$reportPath = Join-Path $reportDirectory $reportName
$loaderArguments += @('--timeout-seconds', [string]$TimeoutSeconds, '--report', $reportPath)
if ($Method) {
    $loaderArguments += @('--method', $Method)
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($ElevateIfRequired -and -not $isAdministrator) {
    # The target's privileges are unknown in advance. Elevate this single watcher before it
    # arms, never after selection and never once per polling attempt.
    $quotedArguments = @($loaderArguments | ForEach-Object { '"' + $_ + '"' })
    Write-Output 'Confirm the one administrator prompt before starting the game. The elevated loader window announces when its watcher is armed.'
    Start-Process -FilePath $loaderPath -ArgumentList $quotedArguments -Verb RunAs -PassThru -Wait | Out-Null
} else {
    & $loaderPath @loaderArguments
}

if (-not (Test-Path -LiteralPath $reportPath)) {
    throw 'The loader did not produce a result.'
}
$result = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$result | Format-List
if (-not $result.success) {
    if ($result.error -eq 5 -and -not $isAdministrator -and -not $ElevateIfRequired) {
        throw "Windows denied access at $($result.stage). Start a fresh watcher with -Watch -ElevateIfRequired for one administrator confirmation before launching a new game process. No entry invocation was retried. See $reportPath"
    }
    throw "Runtime loading did not complete: $($result.stage), Windows error $($result.error), phase $($result.phase). See $reportPath"
}
Write-Output 'The core reports running (plugin worker and overlay hooks started). This does not prove IL2CPP connection or rendered game compatibility. Check dist\logs\ArkFramework.log and the in-game panel.'
