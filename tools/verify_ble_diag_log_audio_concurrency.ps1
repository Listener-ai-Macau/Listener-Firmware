[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "",
    [int]$CaptureSeconds = 6,
    [string]$ArtifactsDir = "",
    [switch]$NoResetBeforeCapture
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_ble_diag_log_audio_concurrency.py"
$defaultArtifactDir = Join-Path $PSScriptRoot "..\tests\artifacts\ble_diag_log"
$resolvedArtifactDir = if ([string]::IsNullOrWhiteSpace($ArtifactsDir)) {
    $defaultArtifactDir
} else {
    $ArtifactsDir
}

$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--device-name", $DeviceName,
    "--capture-seconds", "$CaptureSeconds",
    "--artifacts-dir", $resolvedArtifactDir
)

if (-not [string]::IsNullOrWhiteSpace($BluetoothAddress)) {
    $arguments += @("--bluetooth-address", $BluetoothAddress)
}

if ($NoResetBeforeCapture.IsPresent) {
    $arguments += "--no-reset-before-capture"
}

python @arguments
