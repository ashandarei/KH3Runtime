<#
.SYNOPSIS
Build the self-contained KH3 runtime; optionally install it into KH3.
.DESCRIPTION
Defaults to the local reference MSVC 14.38 toolset. Use -CheckSetup to check
tools and the existing CMake cache without building or copying runtime files.
-VcVarsVer selects another installed toolset explicitly. When changing compilers,
preserve/rename the selected build directory first; a fresh build
directory is configured automatically. -SkipBuild bypasses toolchain checks.
-SyncUe4ss installs an intentionally rebuilt UE4SS DLL into the game only;
normal builds leave the game's loader untouched.
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [switch] $Install,
    [switch] $SyncUe4ss,
    [switch] $EnableDiagnostics,
    [switch] $CheckSetup,
    [ValidatePattern('^14\.[0-9]{2}(\.[0-9]+)?$')]
    [string] $VcVarsVer = "14.38",
    [string] $VsDevCmd = "",
    [string] $Kh3InstallDir = "",
    [string] $CoreHeadersRoot = "",
    [string] $BuildDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot 'kh3-local-environment.ps1')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if (-not $Kh3InstallDir) {
    $Kh3InstallDir = Get-Kh3LocalSetting -Name 'KH3_DIR' -SettingsRoot $RepoRoot
    if ($Kh3InstallDir -and -not [IO.Path]::IsPathRooted($Kh3InstallDir)) {
        $Kh3InstallDir = Join-Path $RepoRoot $Kh3InstallDir
    }
}
if (-not $Kh3InstallDir -and $Install -and -not $CheckSetup) {
    throw 'Set KH3_DIR in the environment/.env, or pass -Kh3InstallDir with the EGS runtime root.'
}
if (-not $CoreHeadersRoot) {
    $CoreHeadersRoot = Get-Kh3LocalSetting -Name 'KH3AP_UE417_CORE_ROOT' -SettingsRoot $RepoRoot
}
if ($CoreHeadersRoot -and -not $SkipBuild) {
    if (-not [IO.Path]::IsPathRooted($CoreHeadersRoot)) {
        $CoreHeadersRoot = Join-Path $RepoRoot $CoreHeadersRoot
    }
    $CoreHeadersRoot = [IO.Path]::GetFullPath($CoreHeadersRoot).Replace('\', '/')
    foreach ($Header in @('Templates/Function.h', 'HAL/UnrealMemory.h', 'Containers/Map.h')) {
        if (-not (Test-Path -LiteralPath (Join-Path $CoreHeadersRoot "Public/$Header") -PathType Leaf)) {
            throw "KH3AP_UE417_CORE_ROOT is missing the licensed Core header: $Header"
        }
    }
}
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot $(if ($EnableDiagnostics) { 'build-diagnostics' } else { 'build' })
}
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $RepoRoot $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
if ($SyncUe4ss -and -not $Install) { throw '-SyncUe4ss requires -Install.' }
$SourceDll = Join-Path $BuildDir "KH3APProbeCpp\main.dll"
$SourceBuildInfo = "$SourceDll.build.json"
$SourceUe4ssDll = Join-Path $BuildDir "Output\Game__Shipping__Win64\UE4SS\bin\UE4SS.dll"



function Copy-RuntimeFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,
        [Parameter(Mandatory = $true)]
        [string] $Target
    )

    New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
    try {
        Copy-Item -LiteralPath $Source -Destination $Target -Force
    }
    catch {
        throw "Failed to copy KH3 runtime file to '$Target'. If this is a live KH3 install path, close KINGDOM HEARTS III and retry, or omit -Install when only building the DLL. $($_.Exception.Message)"
    }
}

function Set-Ue4ssModState {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ModsTxt,
        [Parameter(Mandatory = $true)]
        [string] $ModName,
        [Parameter(Mandatory = $true)]
        [bool] $Enabled,
        [bool] $AddIfMissing = $true
    )

    $Lines = if (Test-Path -LiteralPath $ModsTxt -PathType Leaf) {
        @(Get-Content -LiteralPath $ModsTxt)
    }
    else {
        @()
    }
    $UpdatedLines = New-Object System.Collections.Generic.List[string]
    $ModPattern = "^\s*$([regex]::Escape($ModName))\s*:"
    $ModState = if ($Enabled) { "1" } else { "0" }
    $Found = $false

    foreach ($Line in $Lines) {
        if ($Line -match $ModPattern) {
            $UpdatedLines.Add("${ModName} : $ModState")
            $Found = $true
        }
        else {
            $UpdatedLines.Add($Line)
        }
    }
    if (-not $Found -and $AddIfMissing) {
        if ($UpdatedLines.Count -gt 0 -and $UpdatedLines[$UpdatedLines.Count - 1].Trim()) {
            $UpdatedLines.Add("")
        }
        $UpdatedLines.Add("${ModName} : $ModState")
    }
    if (-not $Found -and -not $AddIfMissing) {
        return
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $ModsTxt) -Force | Out-Null
    $Text = ($UpdatedLines -join "`n") + "`n"
    [System.IO.File]::WriteAllText($ModsTxt, $Text, [System.Text.UTF8Encoding]::new($false))
}

if ($CheckSetup -and $SkipBuild) {
    throw "-CheckSetup checks build tools and cannot be combined with -SkipBuild."
}

if (-not $SkipBuild) {
    $VsDevCmd = Resolve-VsDevCmd -ExplicitPath $VsDevCmd

    if (-not $VsDevCmd -or -not (Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) {
        throw "Visual Studio 2022 with MSVC $VcVarsVer was not found. Install Desktop development with C++ and the MSVC v143 $VcVarsVer x64/x86 tools in Visual Studio Installer (Individual components), or pass -VsDevCmd explicitly."
    }
    $PreflightScript = Join-Path $PSScriptRoot "check-kh3-probe-toolchain.ps1"
    # Keep the host version consistent: a Windows PowerShell child of pwsh can
    # inherit PSModulePath entries that prevent its built-in modules loading.
    $PreflightPowerShell = Join-Path $PSHOME 'pwsh.exe'
    if (-not (Test-Path -LiteralPath $PreflightPowerShell)) {
        $PreflightPowerShell = Join-Path $PSHOME 'powershell.exe'
    }
    $ProbeSourceDir = $RepoRoot
    $DevEnvironment = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -vcvars_ver=$VcVarsVer"
    $RecordArgument = if ($CheckSetup) { '' } else { " -RecordPath `"$(Join-Path $BuildDir 'toolchain.json')`"" }
    $PreflightCommand = "$DevEnvironment && `"$PreflightPowerShell`" -NoProfile -ExecutionPolicy Bypass -File `"$PreflightScript`" -BuildDir `"$BuildDir`" -SourceDir `"$ProbeSourceDir`" -VcVarsVer $VcVarsVer$RecordArgument"
    Invoke-CheckedCommand -FilePath "cmd.exe" -CommandArguments @("/d", "/s", "/c", $PreflightCommand)
    if ($CheckSetup) { return }

    $DiagnosticsValue = if ($EnableDiagnostics) { "ON" } else { "OFF" }
    $RustTools = Resolve-RustTools
    $ProbeSourceDir = $RepoRoot
    $CacheFile = Join-Path $BuildDir "CMakeCache.txt"
    $CacheMatchesMode = (Test-Path -LiteralPath $CacheFile) -and
        (Test-Path -LiteralPath (Join-Path $BuildDir 'build.ninja')) -and
        (Select-String -LiteralPath $CacheFile -Pattern "^KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS:BOOL=$DiagnosticsValue$" -Quiet) -and
        (Select-String -LiteralPath $CacheFile -Pattern '^CMAKE_BUILD_TYPE:STRING=Game__Shipping__Win64$' -Quiet) -and
        (Select-String -LiteralPath $CacheFile -Pattern '^KH3AP_ALLOW_UNPINNED_UE4SS:BOOL=OFF$' -Quiet)
    if ($CacheMatchesMode -and $CoreHeadersRoot) {
        $CacheMatchesMode = Select-String -LiteralPath $CacheFile -Quiet -Pattern (
            '^KH3AP_UE417_CORE_ROOT:PATH=' + [regex]::Escape($CoreHeadersRoot) + '$')
    }
    foreach ($Tool in @('rustc', 'cargo')) {
        if (-not $CacheMatchesMode) { break }
        $Key = if ($Tool -eq 'rustc') { 'Rust_COMPILER' } else { 'Rust_CARGO' }
        $CacheMatchesMode = Select-String -LiteralPath $CacheFile -Quiet -Pattern (
            '^' + $Key + ':[^=]+=' + [regex]::Escape($RustTools[$Tool]) + '$')
    }
    # Avoid re-running dependency configuration for an unchanged build mode.
    # An omitted switch always requests OFF, rather than inheriting cached ON.
    $ConfigureCommand = if ($CacheMatchesMode) { "" } else {
        $CoreHeadersArgument = if ($CoreHeadersRoot) { " `"-DKH3AP_UE417_CORE_ROOT=$CoreHeadersRoot`"" } else { '' }
        $RustArguments = " `"-DRust_COMPILER=$($RustTools['rustc'])`" `"-DRust_CARGO=$($RustTools['cargo'])`" -DRust_RESOLVE_RUSTUP_TOOLCHAINS=OFF"
        "cmake -S `"$ProbeSourceDir`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DKH3AP_ALLOW_UNPINNED_UE4SS=OFF -DKH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS=$DiagnosticsValue$CoreHeadersArgument$RustArguments && "
    }
    $BuildCommand = "$DevEnvironment && ${ConfigureCommand}cmake --build `"$BuildDir`" --target KH3APProbeCpp"
    Invoke-CheckedCommand -FilePath "cmd.exe" -CommandArguments @("/d", "/s", "/c", $BuildCommand)
}

if (-not (Test-Path -LiteralPath $SourceDll)) {
    throw "Built KH3 probe DLL not found: $SourceDll"
}
if ($SyncUe4ss -and -not (Test-Path -LiteralPath $SourceUe4ssDll)) {
    throw "Built pinned UE4SS DLL not found: $SourceUe4ssDll"
}
if (-not (Test-Path -LiteralPath $SourceBuildInfo -PathType Leaf)) {
    throw "Probe build metadata is missing. Run this helper without -SkipBuild before installing."
}
$BuildInfo = Get-Content -LiteralPath $SourceBuildInfo -Raw | ConvertFrom-Json
$SourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $SourceDll).Hash
if ($BuildInfo.schema -ne 3 -or $BuildInfo.performance_diagnostics -isnot [bool] -or
    $BuildInfo.automation_handlers -isnot [bool] -or
    $BuildInfo.automation_handlers -ne $BuildInfo.performance_diagnostics -or
    $BuildInfo.sha256 -ne $SourceHash) {
    throw "Probe build metadata does not match the DLL. Rebuild without -SkipBuild."
}
if ((-not $SkipBuild -or $PSBoundParameters.ContainsKey('EnableDiagnostics')) -and
    $BuildInfo.performance_diagnostics -ne [bool]$EnableDiagnostics) {
    throw "The existing DLL has a different diagnostics mode. Rebuild without -SkipBuild to change it."
}
$BuildMode = if ($BuildInfo.performance_diagnostics) { "ON (diagnostic build)" } else { "OFF (player build)" }
Write-Host "KH3 performance diagnostics: $BuildMode"
Write-Host "KH3 agent automation handlers: $BuildMode"

$CopiedPaths = New-Object System.Collections.Generic.List[string]
$CopiedPaths.Add($SourceDll)
if ($SyncUe4ss) {
    $CopiedPaths.Add($SourceUe4ssDll)
}

if ($Install) {
    $GameInstallDir = [System.IO.Path]::GetFullPath($Kh3InstallDir)
    $DirectGameExe = Join-Path $GameInstallDir "Binaries\Win64\KINGDOM HEARTS III.exe"
    $NestedSteamGameDir = Join-Path $GameInstallDir "KINGDOM HEARTS III"
    $NestedSteamGameExe = Join-Path $NestedSteamGameDir "Binaries\Win64\KINGDOM HEARTS III.exe"
    if (-not (Test-Path -LiteralPath $DirectGameExe -PathType Leaf) -and
        (Test-Path -LiteralPath $NestedSteamGameExe -PathType Leaf)) {
        $GameInstallDir = $NestedSteamGameDir
        Write-Host "Resolved nested Steam KH3 runtime root: $GameInstallDir"
    }

    $GameWin64Dir = Join-Path $GameInstallDir "Binaries\Win64"
    $RootModsDir = Join-Path $GameWin64Dir "Mods"
    $LegacyModsDir = Join-Path $GameWin64Dir "KINGDOM HEARTS III\Mods"
    $GameTargets = @(
        (Join-Path $RootModsDir "KH3APProbeCpp\dlls\main.dll")
    )
    $PathSpecificModsTxt = $null

    $InstallParts = $GameInstallDir -split "[\\/]"
    $IsSteamInstall = $InstallParts -contains "steamapps"
    if (-not $IsSteamInstall) {
        $GameSpecificConfigName = Split-Path -Leaf (Split-Path -Parent $GameInstallDir)
        if ($GameSpecificConfigName -and $GameSpecificConfigName -ne "KINGDOM HEARTS III") {
            $PathSpecificModsDir = Join-Path $GameWin64Dir "$GameSpecificConfigName\Mods"
            $GameTargets += Join-Path $PathSpecificModsDir "KH3APProbeCpp\dlls\main.dll"
            $PathSpecificModsTxt = Join-Path $PathSpecificModsDir "mods.txt"
        }
    }

    foreach ($Target in $GameTargets) {
        Copy-RuntimeFile -Source $SourceDll -Target $Target
        $CopiedPaths.Add($Target)
    }

    if ($PathSpecificModsTxt) {
        $RootProbeConfig = Join-Path $RootModsDir "KH3APProbeCpp\config.lua"
        $PathSpecificProbeConfig = Join-Path $PathSpecificModsDir "KH3APProbeCpp\config.lua"
        if (Test-Path -LiteralPath $RootProbeConfig -PathType Leaf) {
            Copy-RuntimeFile -Source $RootProbeConfig -Target $PathSpecificProbeConfig
            $CopiedPaths.Add($PathSpecificProbeConfig)
        }
    }

    Set-Ue4ssModState `
        -ModsTxt (Join-Path $RootModsDir "mods.txt") `
        -ModName "KH3APProbeCpp" `
        -Enabled $true
    if ($PathSpecificModsTxt) {
        Set-Ue4ssModState `
            -ModsTxt $PathSpecificModsTxt `
            -ModName "KH3APProbeCpp" `
            -Enabled $true
    }
    Set-Ue4ssModState `
        -ModsTxt (Join-Path $LegacyModsDir "mods.txt") `
        -ModName "KH3APProbeCpp" `
        -Enabled $false `
        -AddIfMissing $false

    if ($SyncUe4ss) {
        $GameUe4ssTarget = Join-Path $GameWin64Dir "UE4SS.dll"
        Copy-RuntimeFile -Source $SourceUe4ssDll -Target $GameUe4ssTarget
        $CopiedPaths.Add($GameUe4ssTarget)
    }
}

$CopiedPaths |
    Select-Object -Unique |
    ForEach-Object { Get-FileHash -Algorithm SHA256 -LiteralPath $_ } |
    Select-Object Path, Hash
