param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Target = "esp32s3",
    [string]$BuildDir = $env:LISTENER_IDF_USB_LS_BUILD_DIR,
    [string]$Sdkconfig = $env:LISTENER_IDF_USB_LS_SDKCONFIG,
    [switch]$NoBuild,
    [switch]$PreserveOtaData,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot

function Get-ProjectHash {
    param([Parameter(Mandatory = $true)][string]$ProjectRoot)

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLowerInvariant())
        $hashBytes = $sha1.ComputeHash($bytes)
        return -join ($hashBytes[0..5] | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha1.Dispose()
    }
}

function Resolve-DebugBuildDir {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
    }

    $hash = Get-ProjectHash -ProjectRoot $RepoRoot
    return Join-Path ([System.IO.Path]::GetTempPath()) "listener-idf-build-usb-light-sleep-debug-$hash"
}

function Invoke-CheckedCommand {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$buildDirResolved = Resolve-DebugBuildDir -RepoRoot $projectRoot

if (-not $NoBuild.IsPresent) {
    $buildArgs = @(
        "-NoProfile",
        "-File",
        (Join-Path $PSScriptRoot "build_usb_light_sleep_debug.ps1"),
        "-Target",
        $Target,
        "-BuildDir",
        $buildDirResolved
    )
    if (-not [string]::IsNullOrWhiteSpace($Sdkconfig)) {
        $buildArgs += @("-Sdkconfig", $Sdkconfig)
    }
    if ($Clean.IsPresent) {
        $buildArgs += "-Clean"
    }

    Invoke-CheckedCommand -File "pwsh" -Arguments $buildArgs
}

$flashArgs = @(
    "-NoProfile",
    "-File",
    (Join-Path $PSScriptRoot "flash.ps1"),
    "-Port",
    $Port,
    "-Target",
    $Target,
    "-BuildDir",
    $buildDirResolved,
    "-NoBuild"
)
if ($PreserveOtaData.IsPresent) {
    $flashArgs += "-PreserveOtaData"
}

Invoke-CheckedCommand -File "pwsh" -Arguments $flashArgs
