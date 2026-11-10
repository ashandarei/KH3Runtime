# Initialize pinned dependencies using a local UEPseudo source checkout.
[CmdletBinding()]
param([string] $UePseudoSource = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'kh3-local-environment.ps1')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if (-not $UePseudoSource) {
    $UePseudoSource = Get-Kh3LocalSetting -Name 'KH3AP_UEPSEUDO_SOURCE' -SettingsRoot $ProjectRoot
}
if (-not $UePseudoSource) {
    throw 'Set KH3AP_UEPSEUDO_SOURCE or pass -UePseudoSource with a licensed local UEPseudo Git checkout containing the pinned revision.'
}
if (-not [IO.Path]::IsPathRooted($UePseudoSource)) { $UePseudoSource = Join-Path $ProjectRoot $UePseudoSource }
$UePseudoSource = (Resolve-Path -LiteralPath $UePseudoSource).Path
Invoke-CheckedCommand git @('-C', $UePseudoSource, 'rev-parse', '--git-dir')
$Cmake = Get-Content -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw
if ($Cmake -notmatch 'set\(KH3AP_PINNED_UE4SS_REPOSITORY "(https://github\.com/[^"\s]+)"\)') {
    throw 'Missing reviewed UE4SS repository.'
}
$Repository = $Matches[1]
if ($Cmake -notmatch 'set\(KH3AP_PINNED_UE4SS_COMMIT "([0-9a-f]{40})"\)') { throw 'Missing UE4SS revision pin.' }
$Revision = $Matches[1]
$Destination = Join-Path $ProjectRoot 'vendor/ue4ss-goa'
if (-not (Test-Path -LiteralPath $Destination)) {
    & git -c core.longpaths=true clone -- $Repository $Destination
    if ($LASTEXITCODE -ne 0) { throw 'UE4SS clone failed.' }
    & git -C $Destination checkout --detach $Revision
    if ($LASTEXITCODE -ne 0) { throw 'UE4SS revision checkout failed.' }
} else {
    $Existing = & git -C $Destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $Existing -ne $Revision) {
        throw 'Existing vendor checkout does not match the reviewed revision; preserve it and resolve the mismatch.'
    }
}
$UnrealPath = Join-Path $Destination 'deps/first/Unreal'
if ([IO.Path]::GetFullPath($UnrealPath).TrimEnd('\') -eq $UePseudoSource.TrimEnd('\')) {
    throw 'UePseudoSource must be a separate checkout, not the destination submodule.'
}
$UnrealTree = & git -C $Destination ls-tree HEAD -- deps/first/Unreal
$TreeExit = $LASTEXITCODE
$UnrealPinMatch = [regex]::Match(($UnrealTree -join "`n"), '^160000 commit ([0-9a-f]{40})\s')
if ($TreeExit -ne 0 -or -not $UnrealPinMatch.Success) {
    throw 'The reviewed UE4SS revision has no pinned Unreal submodule.'
}
$UnrealRevision = $UnrealPinMatch.Groups[1].Value
& git -C $UePseudoSource cat-file -e "$UnrealRevision^{commit}"
if ($LASTEXITCODE -ne 0) {
    throw "The local UEPseudo source lacks pinned commit $UnrealRevision. Obtain that exact revision through the licensed source repository; do not substitute its latest branch."
}
# Set the source in Git's local config before submodule initialization.
# This preserves the tracked .gitmodules file and pinned commit.
Invoke-CheckedCommand git @('-C', $Destination, 'config', 'submodule.deps/first/Unreal.url', $UePseudoSource)
if (Test-Path -LiteralPath (Join-Path $UnrealPath '.git')) {
    $UnrealChanges = & git -C $UnrealPath status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0 -or $UnrealChanges) { throw 'Preserve local changes in the Unreal submodule before setup.' }
    Invoke-CheckedCommand git @('-C', $UnrealPath, 'remote', 'set-url', 'origin', $UePseudoSource)
}
Invoke-CheckedCommand git @('-C', $Destination, '-c', 'core.longpaths=true', '-c', 'protocol.file.allow=always',
    'submodule', 'update', '--init', '--', 'deps/first/Unreal')
& git -C $Destination -c core.longpaths=true submodule update --init --recursive --jobs 8
if ($LASTEXITCODE -ne 0) { throw 'UE4SS submodule preparation failed.' }
Write-Host "Reviewed UE4SS sources ready: $Revision"
Write-Host "Licensed local UEPseudo ready: $UnrealRevision"
