[CmdletBinding()]
param(
    [string] $BuildDir = '',
    [string] $VsDevCmd = '',
    [string] $VcVarsVer = '14.38'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot 'build-tests/policies' }
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $ProjectRoot $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$VsDevCmd = Resolve-VsDevCmd -ExplicitPath $VsDevCmd
if (-not $VsDevCmd) { throw 'Visual Studio C++ tools were not found.' }
$Source = Join-Path $ProjectRoot 'tests/policies'
$Command = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -vcvars_ver=$VcVarsVer && cmake -S `"$Source`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build `"$BuildDir`" --target randomization --parallel 4"
Invoke-CheckedCommand -FilePath 'cmd.exe' -CommandArguments @('/d', '/s', '/c', $Command)
Write-Output (Join-Path $BuildDir 'randomization.exe')
