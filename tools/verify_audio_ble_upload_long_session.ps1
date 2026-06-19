[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 60,
    [string]$DeviceName = "listener",
    [switch]$ResetBeforeCapture
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_upload_long_session.py"
$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--capture-seconds", "$CaptureSeconds",
    "--device-name", $DeviceName
)

if (-not $ResetBeforeCapture.IsPresent) {
    $arguments += "--no-reset-before-capture"
}

python @arguments
