function Resolve-RustTools {
    $Rustup = Get-Command rustup.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $Result = @{}
    foreach ($Name in @('rustc', 'cargo')) {
        if ($Rustup) {
            $Resolved = & $Rustup.Source which $Name
            if ($LASTEXITCODE -ne 0) { throw "rustup could not resolve $Name." }
        } else {
            $Command = Get-Command "$Name.exe" -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if (-not $Command) { throw "Missing Rust tool: $Name" }
            $Resolved = $Command.Source
        }
        if (-not (Test-Path -LiteralPath $Resolved -PathType Leaf)) { throw "Invalid Rust executable: $Resolved" }
        $Result[$Name] = [IO.Path]::GetFullPath($Resolved).Replace('\', '/')
    }
    return $Result
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,
        [string[]] $CommandArguments = @()
    )

    & $FilePath @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($CommandArguments -join ' ')"
    }
}

function Resolve-VsDevCmd {
    param(
        [string] $ExplicitPath
    )

    if ($ExplicitPath) {
        return $ExplicitPath
    }

    $VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $VsWhere -PathType Leaf) {
        $InstallPaths = & $VsWhere `
            -version "[17.0,18.0)" `
            -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath

        foreach ($InstallPath in $InstallPaths) {
            $Candidate = Join-Path $InstallPath "Common7\Tools\VsDevCmd.bat"
            if ((Test-Path -LiteralPath $Candidate -PathType Leaf) -and
                (Test-Path -Path (Join-Path $InstallPath "VC\Tools\MSVC\$VcVarsVer*\bin\Hostx64\x64\cl.exe"))) {
                return $Candidate
            }
        }
    }

    $Candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )

    return $Candidates | Where-Object {
        $InstallRoot = Split-Path (Split-Path (Split-Path $_ -Parent) -Parent) -Parent
        (Test-Path -LiteralPath $_ -PathType Leaf) -and
        (Test-Path -Path (Join-Path $InstallRoot "VC\Tools\MSVC\$VcVarsVer*\bin\Hostx64\x64\cl.exe"))
    } | Select-Object -First 1
}
