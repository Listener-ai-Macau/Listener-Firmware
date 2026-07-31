[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$CaptureSeconds = 4,
    [string]$DeviceName = "listener",
    [string]$OutputDir = "",
    [string]$SerialLogPath = "",
    [ValidateSet("serial-toggle", "physical-key", "voice-activation")]
    [string]$TriggerMode = "serial-toggle",
    [int]$MaxSessions = 1,
    [string]$BluetoothAddress = ""
)

$ErrorActionPreference = "Stop"

$pythonScript = Join-Path $PSScriptRoot "capture_audio_ble_wav.py"
$defaultArtifactDir = Join-Path $PSScriptRoot "..\\tests"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $resolvedOutputDir = $defaultArtifactDir
}
else {
    $resolvedOutputDir = $OutputDir
}

if ([string]::IsNullOrWhiteSpace($SerialLogPath)) {
    $resolvedSerialLogPath = Join-Path $resolvedOutputDir "capture_ble_latest.log"
}
else {
    $resolvedSerialLogPath = $SerialLogPath
}

$arguments = @(
    $pythonScript,
    "--port", $Port,
    "--capture-seconds", "$CaptureSeconds",
    "--device-name", $DeviceName,
    "--output-dir", $resolvedOutputDir,
    "--serial-log-path", $resolvedSerialLogPath,
    "--trigger-mode", $TriggerMode,
    "--max-sessions", "$MaxSessions"
)

if (-not [string]::IsNullOrWhiteSpace($BluetoothAddress)) {
    $arguments += @("--bluetooth-address", $BluetoothAddress)
}

python @arguments
