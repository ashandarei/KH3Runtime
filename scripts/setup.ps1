# Fetch only the reviewed open-source dependency. Does not install tools or games.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
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
& git -C $Destination -c core.longpaths=true submodule update --init --recursive --jobs 8
if ($LASTEXITCODE -ne 0) { throw 'UE4SS submodule preparation failed.' }
Write-Host "Reviewed UE4SS sources ready: $Revision"
