<#
.SYNOPSIS
    Builds ArkFramework with the Zig C/C++ toolchain.

.DESCRIPTION
    Targets:
      All     Builds and runs the automated tests, then builds the core, the shared
              ImGui DLL, the loader, the bootstrap DLL, the test host and the plugins.
      Tests   Builds and runs the automated tests only.
      Plugin  Rebuilds the plugins only; requires a previous All build in the same
              dist directory.

    Objects are rebuilt when their source, any project header or this script changes.

.PARAMETER DistDirectory
    Output directory for runtime binaries, relative to the repository or absolute.
    Use a separate directory while a running process still maps the core.

.PARAMETER Release
    Builds without debug information (-g0, stripped, no PDB) into build\release.

.PARAMETER SkipTests
    With -Target All, skips building and running the automated tests.
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'Tests', 'Plugin')]
    [string]$Target = 'All',
    [string]$DistDirectory = 'dist',
    [switch]$Release,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$ImGuiRoot = Join-Path $Root 'third_party\imgui-1.91.9b'
$MinHookRoot = Join-Path $Root 'third_party\minhook-1.3.4'

function Find-Zig {
    $bundled = Join-Path $Root '.tools\zig\ziglang\zig.exe'
    if (Test-Path -LiteralPath $bundled) {
        return $bundled
    }
    $installed = Get-Command zig -ErrorAction SilentlyContinue
    if ($installed) {
        return $installed.Source
    }
    throw 'Zig 0.15.2 was not found. Put it on PATH or at .tools\zig\ziglang\zig.exe.'
}

$Zig = Find-Zig
if (-not $env:ZIG_GLOBAL_CACHE_DIR) {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Root '.tools\zig-cache'
}

$BuildDirectory = Join-Path $Root $(if ($Release) { 'build\release' } else { 'build' })
$ObjectDirectory = Join-Path $BuildDirectory 'obj'
$TestDirectory = Join-Path $BuildDirectory 'tests'
$Dist = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($DistDirectory)) {
            $DistDirectory
        } else {
            Join-Path $Root $DistDirectory
        }))
$PluginDirectory = Join-Path $Dist 'plugins'
foreach ($directory in @($ObjectDirectory, $TestDirectory, $PluginDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$TargetFlags = @('-target', 'x86_64-windows-gnu', '-O2')
$DebugFlags = if ($Release) { @('-g0') } else { @() }
$StripFlags = if ($Release) { @('-s') } else { @() }
$IncludeFlags = @(
    (Join-Path $Root 'include'),
    $ImGuiRoot,
    (Join-Path $MinHookRoot 'include')
) | ForEach-Object { '-I' + $_ }
$TestIncludeFlags = @(
    (Join-Path $Root 'src\core'),
    (Join-Path $Root 'tests\fixtures')
) | ForEach-Object { '-I' + $_ }
$DefineFlags = @('-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN')
$WarningFlags = @(
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-Wno-cast-function-type-mismatch'
)
$CxxFlags = @('-std=c++20') + $WarningFlags + $DefineFlags +
    @('-DIMGUI_USER_CONFIG=<ark/imgui_config.h>') + $IncludeFlags
# Third-party C sources keep their upstream defaults; callers add defines explicitly.
$CFlags = $IncludeFlags

$NewestHeader = Get-ChildItem -Path (Join-Path $Root 'include'), (Join-Path $Root 'src'),
    (Join-Path $Root 'plugins'), (Join-Path $Root 'tests') -Include '*.h' -File -Recurse |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1 -ExpandProperty LastWriteTimeUtc
$ScriptTime = (Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc

function Invoke-Zig([string[]]$Arguments) {
    # Out-Host keeps compiler output out of the function's return values.
    & $Zig @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "zig $($Arguments[0]) failed with exit code $LASTEXITCODE"
    }
}

function Get-Sources([string]$Directory, [string]$Filter = '*.cpp') {
    Get-ChildItem -LiteralPath (Join-Path $Root $Directory) -Filter $Filter -File |
        Sort-Object Name |
        ForEach-Object { Join-Path $Directory $_.Name }
}

# Compiles one source file (relative to the repository) and returns its object path.
# A tag distinguishes several objects built from the same source with different flags.
function Invoke-Compile([string]$Source, [string]$Tag = '', [string[]]$ExtraFlags = @()) {
    $sourcePath = Join-Path $Root $Source
    $object = Join-Path $ObjectDirectory (($Source -replace '[\\/.]', '_') + $Tag + '.o')
    $stale = -not (Test-Path -LiteralPath $object)
    if (-not $stale) {
        $objectTime = (Get-Item -LiteralPath $object).LastWriteTimeUtc
        $stale = (Get-Item -LiteralPath $sourcePath).LastWriteTimeUtc -gt $objectTime -or
            $NewestHeader -gt $objectTime -or $ScriptTime -gt $objectTime
    }
    if ($stale) {
        Write-Host "Compile $Source $Tag"
        if ($Source.EndsWith('.c')) {
            $compiler = @('cc') + $CFlags
        } else {
            $compiler = @('c++') + $CxxFlags
        }
        Invoke-Zig ($compiler + $TargetFlags + $DebugFlags + $ExtraFlags +
            @('-c', $sourcePath, '-o', $object))
    }
    return $object
}

function Invoke-CompileAll([string[]]$Sources, [string[]]$ExtraFlags = @()) {
    foreach ($source in $Sources) {
        Invoke-Compile $source '' $ExtraFlags
    }
}

function New-Executable([string]$Output, [string[]]$Objects, [string[]]$Libraries = @()) {
    Write-Host "Link $Output"
    Invoke-Zig (@('c++') + $TargetFlags + @('-municode') + $StripFlags + $Objects + $Libraries +
        @('-o', $Output))
}

function Get-ImportLibraryFlag([string]$Output) {
    '-Wl,--out-implib,' + (Join-Path $ObjectDirectory ((Split-Path $Output -Leaf) + '.a'))
}

function New-SharedLibrary([string]$Output, [string[]]$Objects, [string[]]$Libraries = @()) {
    Write-Host "Link $Output"
    if (-not ($Libraries -like '-Wl,--out-implib,*')) {
        $Libraries += Get-ImportLibraryFlag $Output
    }
    Invoke-Zig (@('c++') + $TargetFlags + @('-shared') + $StripFlags + $Objects + $Libraries +
        @('-o', $Output))
}

function Invoke-Test([string]$Name, [string[]]$Arguments = @()) {
    & (Join-Path $TestDirectory "$Name.exe") @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

$MinHookObjects = Invoke-CompileAll @(
    'third_party\minhook-1.3.4\src\buffer.c',
    'third_party\minhook-1.3.4\src\hook.c',
    'third_party\minhook-1.3.4\src\trampoline.c',
    'third_party\minhook-1.3.4\src\hde\hde64.c'
) @('-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN')
$CoreSources = @(Get-Sources 'src\core')
$ImGuiImportLibrary = Join-Path $Dist 'libArkImGui.dll.a'

if ($Target -eq 'Tests' -or ($Target -eq 'All' -and -not $SkipTests)) {
    # lifecycle_tests links the plugin runtime without the D3D11 overlay and DLL entry point;
    # native_hook_tests links the native hook gate alone.
    $runtimeSources = $CoreSources | Where-Object {
        $leaf = Split-Path $_ -Leaf
        $leaf -ne 'framework.cpp' -and $leaf -notlike 'overlay*.cpp'
    }
    $nativeHookSources = $CoreSources | Where-Object { (Split-Path $_ -Leaf) -like 'native_hook*.cpp' }

    New-Executable (Join-Path $TestDirectory 'damage_meter_tests.exe') @(
        Invoke-Compile 'tests\damage_meter_tests.cpp')

    $fixture = 'tests\fixtures\fixture_plugin.cpp'
    New-SharedLibrary (Join-Path $TestDirectory 'fixture.dll') @(Invoke-Compile $fixture)
    New-SharedLibrary (Join-Path $TestDirectory 'bad_abi.dll') @(
        Invoke-Compile $fixture '_bad_abi' @('-DTEST_BAD_ABI'))

    # Each lifecycle fixture is a real DLL built from one source with FIXTURE_KIND 1..12.
    $serviceFixture = 'tests\fixtures\lifecycle_service_fixture.cpp'
    $serviceFixtureNames = @(
        'provider', 'consumer', 'intermediate', 'indirect', 'undeclared', 'cycle_a', 'cycle_b',
        'missing_dependency', 'unsafe_dependency', 'failed_init', 'lifetime_provider',
        'failed_init_refuse'
    )
    for ($kind = 1; $kind -le $serviceFixtureNames.Count; $kind++) {
        $object = Invoke-Compile $serviceFixture "_kind$kind" (
            @("-DFIXTURE_KIND=$kind") + $TestIncludeFlags)
        New-SharedLibrary (Join-Path $TestDirectory "$($serviceFixtureNames[$kind - 1]).dll") @($object)
    }
    New-SharedLibrary (Join-Path $TestDirectory 'consumer_updated.dll') @(
        Invoke-Compile $serviceFixture '_consumer_updated' (
            @('-DFIXTURE_KIND=2', '-DFIXTURE_ADDED_DEPENDENCY') + $TestIncludeFlags))

    $lifecycleObjects = @(Invoke-CompileAll $runtimeSources) +
        @(Invoke-Compile 'tests\lifecycle_tests.cpp' '' $TestIncludeFlags) + $MinHookObjects
    New-Executable (Join-Path $TestDirectory 'lifecycle_tests.exe') $lifecycleObjects

    New-SharedLibrary (Join-Path $TestDirectory 'native_hook_fixture.dll') @(
        Invoke-Compile 'tests\fixtures\native_hook_fixture.cpp' '' $TestIncludeFlags)
    $nativeHookObjects = @(Invoke-CompileAll $nativeHookSources) +
        @(Invoke-Compile 'tests\native_hook_tests.cpp' '' $TestIncludeFlags) + $MinHookObjects
    New-Executable (Join-Path $TestDirectory 'native_hook_tests.exe') $nativeHookObjects

    Invoke-Test 'damage_meter_tests'
    Invoke-Test 'lifecycle_tests' @($TestDirectory)
    Invoke-Test 'native_hook_tests' @($TestDirectory)
}

if ($Target -eq 'All') {
    $imguiObjects = Invoke-CompileAll @(
        'third_party\imgui-1.91.9b\imgui.cpp',
        'third_party\imgui-1.91.9b\imgui_draw.cpp',
        'third_party\imgui-1.91.9b\imgui_tables.cpp',
        'third_party\imgui-1.91.9b\imgui_widgets.cpp',
        'third_party\imgui-1.91.9b\backends\imgui_impl_dx11.cpp',
        'third_party\imgui-1.91.9b\backends\imgui_impl_win32.cpp'
    ) @('-DARK_IMGUI_BUILD')
    New-SharedLibrary (Join-Path $Dist 'ArkImGui.dll') $imguiObjects @(
        '-ld3d11', '-ldxgi', '-ldwmapi', '-lgdi32', '-luser32',
        "-Wl,--out-implib,$ImGuiImportLibrary")

    $coreObjects = @(Invoke-CompileAll $CoreSources) + $MinHookObjects
    New-SharedLibrary (Join-Path $Dist 'ArkFramework.dll') $coreObjects @(
        $ImGuiImportLibrary, '-ld3d11', '-ldxgi', '-luser32', '-lgdi32')

    New-Executable (Join-Path $Dist 'ArkFrameworkHost.exe') @(
        Invoke-CompileAll @(Get-Sources 'tests\host')) @(
        '-ld3d11', '-ldxgi', '-lgdi32', '-luser32', '-Wl,--subsystem,windows')

    New-Executable (Join-Path $Dist 'ArkFrameworkLoader.exe') @(
        Invoke-CompileAll @(Get-Sources 'src\loader')) @('-luser32', '-ladvapi32', '-lshell32')

    # The bootstrap is plain C; link it with the C driver so no C++ runtime is pulled in.
    $bootstrap = Join-Path $Dist 'ArkBootstrap.dll'
    $bootstrapObjects = @(Invoke-CompileAll @(Get-Sources 'src\bootstrap' '*.c'))
    Write-Host "Link $bootstrap"
    Invoke-Zig (@('cc') + $TargetFlags + @('-shared') + $StripFlags + $bootstrapObjects +
        @('-luser32', (Get-ImportLibraryFlag $bootstrap), '-o', $bootstrap))
}

if ($Target -ne 'Tests') {
    $damageMeterObjects = Invoke-CompileAll (@(Get-Sources 'plugins\damage_meter') +
        @(Get-Sources 'plugins\common'))
    New-SharedLibrary (Join-Path $PluginDirectory 'damage_meter.dll') $damageMeterObjects @(
        $ImGuiImportLibrary, '-luser32')

    $betterBattleObjects = Invoke-CompileAll (@(Get-Sources 'plugins\BetterBattle') +
        @(Get-Sources 'plugins\common'))
    New-SharedLibrary (Join-Path $PluginDirectory 'BetterBattle.dll') $betterBattleObjects @(
        $ImGuiImportLibrary, '-luser32')

    $webBridgeObjects = Invoke-CompileAll @(Get-Sources 'plugins\web_bridge')
    New-SharedLibrary (Join-Path $PluginDirectory 'web_bridge.dll') $webBridgeObjects @(
        $ImGuiImportLibrary, '-luser32')

    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($dotnet) {
        $webProject = Join-Path $Root 'webview\BetterBattleWeb.csproj'
        $webOutput = Join-Path $Dist 'BetterBattleWeb'
        & $dotnet.Source publish $webProject -c Release -r win-x64 --self-contained false `
            -o $webOutput
    }
}

$LocalBuild = Join-Path $Root 'build.local.ps1'
if (Test-Path -LiteralPath $LocalBuild) {
    . $LocalBuild
}

Write-Host "Build complete: $Dist"
