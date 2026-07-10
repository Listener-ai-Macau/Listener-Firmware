[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$DeviceName = "listener",
    [string]$BluetoothAddress = "DCB4D91112CE",
    [int]$MaintainConnectionDurationSeconds = 12,
    [int]$PollIntervalSeconds = 2,
    [switch]$RestartPanAdapter = $false
)

$ErrorActionPreference = "Stop"

$restart_script = Join-Path $PSScriptRoot "restart_windows_bluetooth.ps1"
$ensure_script = Join-Path $PSScriptRoot "ensure_ble_hid_connection.ps1"

Write-Host "recover_ble_hid_host: restarting Windows Bluetooth stack"
& pwsh -NoProfile -File $restart_script @(
    if ($RestartPanAdapter) { "-RestartPanAdapter" }
)
if ($LASTEXITCODE -ne 0) {
    throw "recover_ble_hid_host: restart_windows_bluetooth failed exit_code=$LASTEXITCODE"
}

Write-Host "recover_ble_hid_host: requesting GATT maintain-connection"
& pwsh -NoProfile -File $ensure_script `
    -DeviceName $DeviceName `
    -BluetoothAddress $BluetoothAddress `
    -DurationSeconds $MaintainConnectionDurationSeconds `
    -PollIntervalSeconds $PollIntervalSeconds `
    -ExitOnReady
if ($LASTEXITCODE -ne 0) {
    throw "recover_ble_hid_host: ensure_ble_hid_connection failed exit_code=$LASTEXITCODE"
}

Write-Host "recover_ble_hid_host: recovery complete"
