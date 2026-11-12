<#
.SYNOPSIS
Build and package a runtime release from a committed source revision.
.DESCRIPTION
Creates a fresh checkout, runs catalog checks and native tests, and builds the
player DLL. Writes the archive, provenance record and log to a new run directory.
PublishMode selects local output, a GitHub draft, or a published release.
#>
[CmdletBinding()]
param(
    [string] $SourceRef = 'HEAD',
    [string] $ReleaseTag = '',
    [ValidateSet('Beta', 'Stable')][string] $Channel = 'Beta',
    [ValidateSet('None', 'Draft', 'Publish')][string] $PublishMode = 'None',
    [switch] $ValidateDiagnostics,
    [string] $OutputRoot = '',
    [string] $NotesFile = '',
    [ValidatePattern('^14\.[0-9]{2}(\.[0-9]+)?$')][string] $VcVarsVer = '14.38',
    [string] $VsDevCmd = '',
    [string] $CoreHeadersRoot = '',
    [string] $UePseudoSource = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'toolchain.ps1')
. (Join-Path $PSScriptRoot 'kh3-local-environment.ps1')

function Read-Git {
    param([string] $Root, [string[]] $Arguments)
    $Result = & git -C $Root @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $($Arguments -join ' ')" }
    return ($Result -join "`n").Trim()
}

if (Read-Git $ProjectRoot @('status', '--porcelain', '--untracked-files=all')) {
    throw 'Commit the intended release changes first; release builds require a clean working tree.'
}
if ($SourceRef.StartsWith('-')) { throw 'SourceRef must be a Git revision, not an option.' }
$Commit = Read-Git $ProjectRoot @('rev-parse', '--verify', '--end-of-options', "$SourceRef^{commit}")
$Version = Read-Git $ProjectRoot @('show', "${Commit}:VERSION.txt")
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'The selected source has an invalid VERSION.txt.' }
if (-not $ReleaseTag) { $ReleaseTag = $Version }
$TagPattern = '^v?' + [regex]::Escape($Version) + '(?:-beta\.[1-9][0-9]*)?$'
if ($ReleaseTag -cnotmatch $TagPattern -or ($Channel -eq 'Stable' -and $ReleaseTag.Contains('-'))) {
    throw 'ReleaseTag must match VERSION.txt (optionally v-prefixed or -beta.N for beta releases).'
}
$ExistingTag = & git -C $ProjectRoot rev-parse --verify --quiet "refs/tags/$ReleaseTag^{commit}"
if ($LASTEXITCODE -eq 0 -and $ExistingTag -ne $Commit) { throw 'The release tag points at a different commit.' }
if (-not $CoreHeadersRoot) {
    $CoreHeadersRoot = Get-Kh3LocalSetting -Name 'KH3AP_UE417_CORE_ROOT' -SettingsRoot $ProjectRoot
}
if (-not $CoreHeadersRoot) { throw 'Set KH3AP_UE417_CORE_ROOT or pass -CoreHeadersRoot.' }
if (-not [IO.Path]::IsPathRooted($CoreHeadersRoot)) { $CoreHeadersRoot = Join-Path $ProjectRoot $CoreHeadersRoot }
$CoreHeadersRoot = (Resolve-Path -LiteralPath $CoreHeadersRoot).Path
if (-not $UePseudoSource) {
    $UePseudoSource = Get-Kh3LocalSetting -Name 'KH3AP_UEPSEUDO_SOURCE' -SettingsRoot $ProjectRoot
}
if (-not $UePseudoSource) { throw 'Set KH3AP_UEPSEUDO_SOURCE or pass -UePseudoSource with the licensed local checkout.' }
if (-not [IO.Path]::IsPathRooted($UePseudoSource)) { $UePseudoSource = Join-Path $ProjectRoot $UePseudoSource }
$UePseudoSource = (Resolve-Path -LiteralPath $UePseudoSource).Path
if ($NotesFile) { $NotesFile = (Resolve-Path -LiteralPath $NotesFile).Path }
if (-not $OutputRoot) { $OutputRoot = Join-Path $ProjectRoot 'artifacts/releases' }
if (-not [IO.Path]::IsPathRooted($OutputRoot)) { $OutputRoot = Join-Path $ProjectRoot $OutputRoot }
$RunRoot = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) (
    "$ReleaseTag-$($Commit.Substring(0, 12))-$([guid]::NewGuid().ToString('N').Substring(0, 8))")
if (Test-Path -LiteralPath $RunRoot) { throw "Release run already exists: $RunRoot" }
New-Item -ItemType Directory -Path $RunRoot > $null
$Source = Join-Path $RunRoot 'source'
$Build = Join-Path $RunRoot 'build-player'
$Output = Join-Path $RunRoot 'dist'
$Transcript = Join-Path $RunRoot 'release.log'
Start-Transcript -LiteralPath $Transcript > $null
try {
    Write-Host "Release source: $Commit; version $Version; tag $ReleaseTag; channel $Channel"
    Invoke-CheckedCommand git @('clone', '--no-local', '--no-checkout', '--', $ProjectRoot, $Source)
    Invoke-CheckedCommand git @('-C', $Source, 'checkout', '--detach', $Commit)
    $RecordScript = Join-Path $Source 'scripts/release_record.py'
    if (-not (Test-Path -LiteralPath $RecordScript)) {
        throw 'The selected commit does not contain the local release tooling. Select a commit that includes it.'
    }
    $Before = Join-Path $RunRoot 'inputs-before.json'
    Invoke-CheckedCommand python @($RecordScript, 'snapshot', '--repo', $Source,
        '--headers', $CoreHeadersRoot, '--output', $Before)
    Push-Location $Source
    try {
        & ./scripts/setup.ps1 -UePseudoSource $UePseudoSource
        Invoke-CheckedCommand python @('scripts/generate_catalogs.py', '--check')
        & ./scripts/test.ps1 -BuildDir (Join-Path $RunRoot 'build-tests') -VcVarsVer $VcVarsVer -VsDevCmd $VsDevCmd
        if ($ValidateDiagnostics) {
            & ./scripts/build.ps1 -BuildDir (Join-Path $RunRoot 'build-diagnostics') -EnableDiagnostics `
                -VcVarsVer $VcVarsVer -VsDevCmd $VsDevCmd -CoreHeadersRoot $CoreHeadersRoot
        }
        & ./scripts/build.ps1 -BuildDir $Build -VcVarsVer $VcVarsVer -VsDevCmd $VsDevCmd -CoreHeadersRoot $CoreHeadersRoot
        $Record = Join-Path $Build 'release-provenance.json'
        $RecordArguments = @($RecordScript, 'record', '--repo', $Source, '--headers', $CoreHeadersRoot,
            '--build', $Build, '--before', $Before, '--output', $Record, '--tag', $ReleaseTag, '--channel', $Channel)
        if ($ValidateDiagnostics) { $RecordArguments += '--diagnostics-validated' }
        Invoke-CheckedCommand python $RecordArguments
        & ./scripts/package.ps1 -BuildDir $Build -OutputDir $Output -ProvenancePath $Record
    } finally { Pop-Location }
    $Archive = @(Get-ChildItem -LiteralPath $Output -Filter '*.zip' -File)
    if ($Archive.Count -ne 1) { throw 'Expected exactly one release archive.' }
    $Archive = $Archive[0]
    $ArchiveHash = (Get-FileHash -LiteralPath $Archive.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $Summary = [ordered]@{
        source_commit = $Commit; version = $Version; release_tag = $ReleaseTag; channel = $Channel
        archive = $Archive.Name; archive_sha256 = $ArchiveHash
        publish_mode = $PublishMode; published_url = $null
    }
    if ($PublishMode -ne 'None') {
        $Remote = Read-Git $ProjectRoot @('remote', 'get-url', 'origin')
        if ($Remote -notmatch '^(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?$') {
            throw 'Publishing requires a github.com origin remote.'
        }
        $Repository = $Matches[1]
        $RemoteCommit = & gh api "repos/$Repository/commits/$Commit" --jq '.sha'
        if ($LASTEXITCODE -ne 0 -or $RemoteCommit -ne $Commit) { throw 'Push the selected commit before publishing.' }
        $TagRefs = Read-Git $ProjectRoot @('ls-remote', 'origin', "refs/tags/$ReleaseTag", "refs/tags/$ReleaseTag^{}")
        if ($TagRefs) {
            $Lines = $TagRefs -split "`n"
            $TagCommit = ($Lines[-1] -split '\s+')[0]
            if ($TagCommit -ne $Commit) { throw 'The remote release tag points at a different commit.' }
        }
        $Notes = Join-Path $RunRoot 'release-notes.md'
        $Text = if ($NotesFile) { [IO.File]::ReadAllText($NotesFile) }
            else { "KH3 Runtime $ReleaseTag`n" }
        [IO.File]::WriteAllText($Notes, $Text, [Text.UTF8Encoding]::new($false))
        $ReleaseArguments = @('release', 'create', $ReleaseTag, $Archive.FullName,
            '--repo', $Repository, '--target', $Commit,
            '--title', "KH3 Runtime $ReleaseTag", '--notes-file', $Notes)
        if ($Channel -eq 'Beta') { $ReleaseArguments += @('--prerelease', '--latest=false') }
        else { $ReleaseArguments += '--latest' }
        if ($PublishMode -eq 'Draft') { $ReleaseArguments += '--draft' }
        # Creation fails for an existing release, preserving its published assets.
        Invoke-CheckedCommand gh $ReleaseArguments
        $Summary.published_url = "https://github.com/$Repository/releases/tag/$ReleaseTag"
    }
    [IO.File]::WriteAllText((Join-Path $Output 'release-summary.json'),
        ($Summary | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    Write-Host "Release artifacts: $Output"
} finally {
    Stop-Transcript > $null
}
