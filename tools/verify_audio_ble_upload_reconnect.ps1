[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 10,
    [string]$DeviceName = "listener",
    [switch]$RestartPanAdapter,
    [switch]$ResetBeforeCapture
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_upload_reconnect.py"
$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--capture-seconds", $CaptureSeconds,
    "--device-name", $DeviceName
)

if (-not $ResetBeforeCapture.IsPresent) {
    $arguments += "--no-reset-before-capture"
}

if ($RestartPanAdapter) {
    $arguments += "--restart-pan-adapter"
}

python @arguments
