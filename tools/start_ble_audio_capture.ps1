[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$DeviceName = "Listener Keyboard",
    [string]$OutputDir = "",
    [string]$SerialLogPath = "",
    [int]$MaxSessions = 1
)

$ErrorActionPreference = "Stop"

$captureScript = Join-Path $PSScriptRoot "capture_audio_ble_wav.ps1"

Write-Host "Starting BLE audio capture..."
Write-Host "Waiting for BLE notify subscription to become ready..."

$params = @{
    Port = $Port
    DeviceName = $DeviceName
    TriggerMode = "physical-key"
    MaxSessions = $MaxSessions
}

if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $params.OutputDir = $OutputDir
}

if (-not [string]::IsNullOrWhiteSpace($SerialLogPath)) {
    $params.SerialLogPath = $SerialLogPath
}

& $captureScript @params
