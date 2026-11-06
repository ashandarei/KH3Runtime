[CmdletBinding()]
param(
    [ValidateSet('all', 'services', 'policies')][string] $Suite = 'all',
    [ValidateRange(1, 32)][int] $Jobs = 4,
    [string] $BuildDir = '',
    [string] $VsDevCmd = '',
    [string] $VcVarsVer = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
Invoke-CheckedCommand -FilePath 'python' -CommandArguments @('-m', 'unittest', 'discover', '-s',
    (Join-Path $ProjectRoot 'tests'), '-p', 'test_*.py')
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot 'build-tests' }
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $ProjectRoot $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$VsDevCmd = Resolve-VsDevCmd -ExplicitPath $VsDevCmd
if (-not $VsDevCmd) { throw 'Visual Studio C++ tools were not found.' }
$DevEnvironment = "`"$VsDevCmd`" -arch=x64 -host_arch=x64"
if ($VcVarsVer) { $DevEnvironment += " -vcvars_ver=$VcVarsVer" }
$Suites = if ($Suite -eq 'all') { @('services', 'policies') } else { @($Suite) }
foreach ($Selected in $Suites) {
    $Source = Join-Path $ProjectRoot $(if ($Selected -eq 'services') { 'tests' } else { 'tests/policies' })
    $Output = Join-Path $BuildDir $Selected
    $Command = "$DevEnvironment && cmake -S `"$Source`" -B `"$Output`" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build `"$Output`" --parallel $Jobs && ctest --test-dir `"$Output`" --output-on-failure --parallel $Jobs"
    Invoke-CheckedCommand -FilePath 'cmd.exe' -CommandArguments @('/d', '/s', '/c', $Command)
}
