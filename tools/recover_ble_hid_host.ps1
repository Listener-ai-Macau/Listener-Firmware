[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "Listener Keyboard",
    [string]$BluetoothAddress = "DCB4D91112CE",
    [int]$MaintainConnectionDurationSeconds = 12,
    [int]$PollIntervalSeconds = 2,
    [switch]$RestartPanAdapter = $false
)

$ErrorActionPreference = "Stop"

$restart_script = Join-Path $PSScriptRoot "restart_windows_bluetooth.ps1"
$ensure_script = Join-Path $PSScriptRoot "ensure_ble_hid_connection.ps1"

Write-Host "recover_ble_hid_host: restarting Windows Bluetooth stack"
& powershell -ExecutionPolicy Bypass -File $restart_script @(
    if ($RestartPanAdapter) { "-RestartPanAdapter" }
)

Write-Host "recover_ble_hid_host: requesting GATT maintain-connection"
& powershell -ExecutionPolicy Bypass -File $ensure_script `
    -DeviceName $DeviceName `
    -BluetoothAddress $BluetoothAddress `
    -DurationSeconds $MaintainConnectionDurationSeconds `
    -PollIntervalSeconds $PollIntervalSeconds `
    -ExitOnReady

Write-Host "recover_ble_hid_host: recovery complete"
