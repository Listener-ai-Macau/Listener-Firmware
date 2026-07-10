param(
    [string]$IdfPath = "",
    [string]$IdfRef = "release/v5.5",
    [string[]]$Targets = @("esp32s3"),
    [switch]$InstallPrerequisites = $true,
    [switch]$UpdateExisting
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    $IdfPath = Join-Path $HOME "esp\esp-idf"
}

function Find-ExecutablePath {
    param(
        [string]$CommandName,
        [string[]]$CandidatePaths
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Path)) {
        return $command.Path
    }

    foreach ($candidate in $CandidatePaths) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Ensure-ExecutablePath {
    param(
        [string]$Name,
        [string]$WingetId,
        [string]$CommandName,
        [string[]]$CandidatePaths
    )

    $path = Find-ExecutablePath -CommandName $CommandName -CandidatePaths $CandidatePaths
    if ($null -ne $path) {
        return $path
    }

    if (-not $InstallPrerequisites) {
        throw "$Name not found. Install it first or rerun setup with -InstallPrerequisites."
    }

    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw "$Name not found and winget is unavailable. Install $Name manually, then rerun setup."
    }

    Write-Host "Installing $Name with winget..."
    & $winget.Path install --id $WingetId -e --source winget --accept-package-agreements --accept-source-agreements

    $path = Find-ExecutablePath -CommandName $CommandName -CandidatePaths $CandidatePaths
    if ($null -eq $path) {
        throw "$Name install completed but executable was not found in this shell. Open a new terminal and rerun setup."
    }

    return $path
}

function Test-IdfReady {
    param(
        [string]$Path
    )

    $exportScript = Join-Path $Path "export.ps1"
    if (-not (Test-Path $exportScript)) {
        return $false
    }

    $idfWrapper = Join-Path $PSScriptRoot "idf.ps1"
    $verifyScript = "& '$idfWrapper' --version"
    $verify = Start-Process -FilePath "pwsh" `
        -ArgumentList @("-NoProfile", "-Command", $verifyScript) `
        -NoNewWindow -Wait -PassThru

    return $verify.ExitCode -eq 0
}

$git_path = Ensure-ExecutablePath `
    -Name "Git" `
    -WingetId "Git.Git" `
    -CommandName "git" `
    -CandidatePaths @(
        "C:\Program Files\Git\cmd\git.exe",
        "C:\Program Files\Git\bin\git.exe"
    )

$python_path = Ensure-ExecutablePath `
    -Name "Python 3.10" `
    -WingetId "Python.Python.3.10" `
    -CommandName "python" `
    -CandidatePaths @(
        (Join-Path $env:LOCALAPPDATA "Programs\Python\Python310\python.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Python\Python311\python.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Python\Python312\python.exe")
    )

Write-Host "Git: $git_path"
Write-Host "Python: $python_path"
Write-Host "ESP-IDF path: $IdfPath"

$idf_root = Split-Path -Parent $IdfPath
New-Item -ItemType Directory -Force -Path $idf_root | Out-Null

if (-not (Test-Path $IdfPath)) {
    Write-Host "Cloning ESP-IDF $IdfRef..."
    & $git_path clone --recursive --branch $IdfRef https://github.com/espressif/esp-idf.git $IdfPath
} elseif (-not (Test-Path (Join-Path $IdfPath ".git"))) {
    throw "Existing path is not an ESP-IDF git checkout: $IdfPath"
} else {
    Write-Host "Existing ESP-IDF checkout found."
    if ($UpdateExisting) {
        Write-Host "Updating existing checkout to $IdfRef..."
        & $git_path -C $IdfPath fetch --all --tags
        & $git_path -C $IdfPath checkout $IdfRef
        & $git_path -C $IdfPath pull --ff-only
    }
}

Write-Host "Syncing submodules..."
& $git_path -C $IdfPath submodule update --init --recursive

[Environment]::SetEnvironmentVariable("ESP_IDF_PATH", $IdfPath, "User")

if (-not (Test-IdfReady -Path $IdfPath)) {
    Write-Host "Installing ESP-IDF tools and Python environment..."
    & (Join-Path $IdfPath "install.ps1") @Targets
} else {
    Write-Host "ESP-IDF tools already look ready. Skipping install."
}

if (-not (Test-IdfReady -Path $IdfPath)) {
    throw "ESP-IDF setup verification failed."
}

Write-Host ""
Write-Host "Setup complete."
Write-Host "Next steps:"
Write-Host "  pwsh -NoProfile -File .\tools\build.ps1"
Write-Host "  pwsh -NoProfile -File .\tools\flash.ps1 -Port COM5"
Write-Host "  pwsh -NoProfile -File .\tools\monitor.ps1 -Port COM5"
