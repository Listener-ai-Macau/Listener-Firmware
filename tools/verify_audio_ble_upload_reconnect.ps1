[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 10,
    [string]$DeviceName = "listener",
    [switch]$RestartPanAdapter
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "verify_audio_ble_upload_reconnect.py"
$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--capture-seconds", $CaptureSeconds,
    "--device-name", $DeviceName
)

if ($RestartPanAdapter) {
    $arguments += "--restart-pan-adapter"
}

python @arguments
