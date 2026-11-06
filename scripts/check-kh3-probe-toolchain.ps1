[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $BuildDir,
    [Parameter(Mandatory = $true)][string] $SourceDir,
    [Parameter(Mandatory = $true)][string] $VcVarsVer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Run inside the same VsDevCmd environment used for configure and build.
if (-not $env:VCToolsVersion -or
    -not ($env:VCToolsVersion.TrimEnd('\') -match "^$([regex]::Escape($VcVarsVer))(\.|$)")) {
    throw "MSVC $VcVarsVer was requested, but VsDevCmd selected '$env:VCToolsVersion'. Install the requested toolset in Visual Studio Installer."
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'The KH3 probe requires an x64 developer environment.' }
if ($VcVarsVer -ne '14.38') {
    Write-Warning "The local reference build uses MSVC 14.38; $VcVarsVer is an explicit override and has not been validated by this check."
}

$ToolPaths = @{}
foreach ($Name in @('cl.exe', 'link.exe', 'rc.exe', 'cmake.exe', 'ninja.exe', 'git.exe', 'cargo.exe', 'rustc.exe')) {
    $Command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $Command) {
        throw "Missing $Name in the selected developer environment. Install VS C++ tools/Windows SDK/CMake tools, Git, and Rust with Cargo as applicable, then retry -CheckSetup."
    }
    $ToolPaths[$Name] = $Command.Source
    Write-Host "${Name}: $($Command.Source)"
}
$ExpectedCompiler = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\cl.exe'
if (-not (Test-Path -LiteralPath $ExpectedCompiler -PathType Leaf) -or
    (Get-FileHash -LiteralPath $ExpectedCompiler).Hash -ne (Get-FileHash -LiteralPath $ToolPaths['cl.exe']).Hash) {
    throw "PATH selected a compiler outside the requested MSVC $VcVarsVer toolset. Open a clean terminal and retry -CheckSetup."
}
foreach ($Name in @('cmake.exe', 'ninja.exe', 'git.exe', 'cargo.exe', 'rustc.exe')) {
    $VersionOutput = & $ToolPaths[$Name] --version
    if ($LASTEXITCODE -ne 0) { throw "$Name could not run successfully in the selected environment." }
    Write-Host ($VersionOutput -join "`n")
    if ($Name -eq 'cmake.exe' -and
        (($VersionOutput -join ' ') -notmatch 'cmake version (\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.22.0')) {
        throw 'CMake 3.22 or newer is required.'
    }
    if ($Name -eq 'rustc.exe') {
        $RustDetails = & $ToolPaths[$Name] -vV
        if ($LASTEXITCODE -ne 0 -or ($RustDetails -join "`n") -notmatch '(?m)^host: x86_64-pc-windows-msvc\s*$') {
            throw 'Rust must use the x86_64-pc-windows-msvc host toolchain for this build.'
        }
    }
}

$CacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $CacheFile) {
    $Cache = @{}
    foreach ($Line in Get-Content -LiteralPath $CacheFile) {
        if ($Line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $Cache[$Matches[1]] = $Matches[2] }
    }
    $Remedy = "Preserve/rename '$BuildDir' and rerun the helper to configure a fresh build. No files have been removed."
    if ($Cache['CMAKE_GENERATOR'] -ne 'Ninja') { throw "The cached generator is not Ninja. $Remedy" }
    if ($Cache['CMAKE_BUILD_TYPE'] -ne 'Game__Shipping__Win64') {
        Write-Warning "Cached build type '$($Cache['CMAKE_BUILD_TYPE'])' is not Game__Shipping__Win64. A normal helper build will reconfigure it automatically; generic Debug/Release types omit the required Unreal definitions."
    }
    if ($Cache['CMAKE_HOME_DIRECTORY'] -and
        [IO.Path]::GetFullPath($Cache['CMAKE_HOME_DIRECTORY']) -ne [IO.Path]::GetFullPath($SourceDir)) {
        throw "The CMake cache belongs to another source directory. $Remedy"
    }
    foreach ($Key in @('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM')) {
        $CachedPath = $Cache[$Key]
        if (-not $CachedPath -or -not (Test-Path -LiteralPath $CachedPath -PathType Leaf)) {
            throw "The cached $Key path is missing: '$CachedPath'. $Remedy"
        }
        if ($Key -ne 'CMAKE_MAKE_PROGRAM' -and
            (Get-FileHash -LiteralPath $CachedPath).Hash -ne (Get-FileHash -LiteralPath $ToolPaths['cl.exe']).Hash) {
            throw "The cached $Key ('$CachedPath') differs from selected MSVC $VcVarsVer ('$($ToolPaths['cl.exe'])'). $Remedy"
        }
    }
} else {
    Write-Host 'No CMake cache yet; the build will configure a fresh Ninja directory.'
}
Write-Host "KH3 toolchain preflight passed (MSVC $env:VCToolsVersion). This checks setup, not whether the source compiles."
