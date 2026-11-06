[CmdletBinding()]
param([string] $BuildDir = '', [string] $OutputDir = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot 'build' }
if (-not $OutputDir) { $OutputDir = Join-Path $ProjectRoot 'dist' }
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $ProjectRoot $BuildDir }
if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path $ProjectRoot $OutputDir }
$Dll = Join-Path $BuildDir 'KH3APProbeCpp/main.dll'
$InfoPath = "$Dll.build.json"
if (-not (Test-Path -LiteralPath $InfoPath)) { throw 'Build the player DLL before packaging.' }
$Info = Get-Content -LiteralPath $InfoPath -Raw | ConvertFrom-Json
$DllHash = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Info.schema -ne 3 -or $Info.performance_diagnostics -isnot [bool] -or
    $Info.automation_handlers -isnot [bool] -or $Info.performance_diagnostics -or
    $Info.automation_handlers -or $Info.sha256 -ne $DllHash -or
    $Info.first_party_license -ne 'MIT') {
    throw 'Release packaging requires a hash-verified MIT player build with diagnostics and automation off.'
}
$PinMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw),
    'set\(KH3AP_PINNED_UE4SS_COMMIT "([0-9a-f]{40})"\)')
$RequiredProvenance = @('ue4ss_revision', 'ue4ss_dirty', 'allow_unpinned_ue4ss', 'release_eligible')
foreach ($Name in $RequiredProvenance) {
    if (-not $Info.PSObject.Properties[$Name]) { throw 'Release packaging requires reviewed UE4SS provenance.' }
}
if (-not $PinMatch.Success -or $Info.ue4ss_revision -cne $PinMatch.Groups[1].Value -or
    $Info.ue4ss_dirty -isnot [bool] -or $Info.ue4ss_dirty -or
    $Info.allow_unpinned_ue4ss -isnot [bool] -or $Info.allow_unpinned_ue4ss -or
    $Info.release_eligible -isnot [bool] -or -not $Info.release_eligible) {
    throw 'Release packaging requires a clean reviewed UE4SS build without research overrides.'
}
$InputsHash = (Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'data/runtime-inputs.json') -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Info.runtime_inputs_sha256 -ne $InputsHash) { throw 'The DLL was built against different runtime catalogs.' }
$Version = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'VERSION.txt') -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION.txt must contain major.minor.patch.' }
New-Item -ItemType Directory -Path $OutputDir -Force > $null
$ArchivePath = Join-Path $OutputDir "KH3Runtime-$Version-$($DllHash.Substring(0, 12)).zip"
if (Test-Path -LiteralPath $ArchivePath) { throw "Package already exists: $ArchivePath" }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Files = [ordered]@{
    'KH3APProbeCpp/dlls/main.dll' = $Dll
    'KH3APProbeCpp/dlls/main.dll.build.json' = $InfoPath
    'LICENSE' = (Join-Path $ProjectRoot 'LICENSE')
    'THIRD_PARTY_NOTICES.md' = (Join-Path $ProjectRoot 'THIRD_PARTY_NOTICES.md')
}
$LicenseRoot = Join-Path $ProjectRoot 'licenses'
foreach ($File in Get-ChildItem -LiteralPath $LicenseRoot -File -Recurse) {
    $Relative = $File.FullName.Substring($LicenseRoot.Length).TrimStart('\', '/').Replace('\', '/')
    $Files["licenses/$Relative"] = $File.FullName
}
$Temporary = "$ArchivePath.$([guid]::NewGuid()).tmp"
$Archive = [IO.Compression.ZipFile]::Open($Temporary, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($Entry in $Files.GetEnumerator()) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($Archive, $Entry.Value, $Entry.Key,
            [IO.Compression.CompressionLevel]::Optimal) > $null
    }
    $Entry = $Archive.CreateEntry('manifest.json')
    $Stream = [IO.StreamWriter]::new($Entry.Open(), [Text.UTF8Encoding]::new($false))
    try {
        $Stream.Write((@{
            schema = 1; version = $Version; mod_id = 'KH3APProbeCpp'; sha256 = $DllHash
            runtime_inputs_sha256 = $InputsHash; first_party_license = 'MIT'
            performance_diagnostics = $false; automation_handlers = $false
        } | ConvertTo-Json))
    } finally { $Stream.Dispose() }
} finally { $Archive.Dispose() }
Move-Item -LiteralPath $Temporary -Destination $ArchivePath
$ArchiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$ArchivePath.sha256", "$ArchiveHash  $([IO.Path]::GetFileName($ArchivePath))`n",
    [Text.UTF8Encoding]::new($false))
Write-Host "DLL release: $ArchivePath"
Write-Host "SHA256: $ArchiveHash"
