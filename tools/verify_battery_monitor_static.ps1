param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

$batteryMonitor = Join-Path $RepoRoot "components\battery_monitor\battery_monitor.c"
if (-not (Test-Path -LiteralPath $batteryMonitor)) {
    throw "Missing battery monitor source: $batteryMonitor"
}

$text = Get-Content -LiteralPath $batteryMonitor -Raw
$bleHidPath = Join-Path $RepoRoot "ports\esp32\ble_hid\ble_hid.c"
$diagEventsPath = Join-Path $RepoRoot "components\diag_log\include\diag_log_events.h"
if (-not (Test-Path -LiteralPath $bleHidPath)) {
    throw "Missing BLE HID source: $bleHidPath"
}
if (-not (Test-Path -LiteralPath $diagEventsPath)) {
    throw "Missing diag log events header: $diagEventsPath"
}
$bleHid = Get-Content -LiteralPath $bleHidPath -Raw
$diagEvents = Get-Content -LiteralPath $diagEventsPath -Raw

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_ABSOLUTE_MIN_MV 2700U\r?$') {
    throw "Battery absolute minimum marker must remain 2700mV."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_EMPTY_MV 3000U\r?$') {
    throw "Battery product empty voltage must be 3000mV."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_FULL_MV 4200U\r?$') {
    throw "Battery full voltage must be 4200mV."
}

if ($text -notmatch 'battery_mv <= BATTERY_MONITOR_EMPTY_MV[\s\S]*return 0;') {
    throw "Battery percentage must clamp at empty voltage."
}

if ($text -notmatch 'battery_mv >= BATTERY_MONITOR_FULL_MV[\s\S]*return 100;') {
    throw "Battery percentage must clamp at full voltage."
}

if ($text -notmatch 'BATTERY_MONITOR_FULL_MV - BATTERY_MONITOR_EMPTY_MV') {
    throw "Battery percentage must derive range from configured endpoints."
}

if ($bleHid -notmatch '#define BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT 1\b') {
    throw "BLE HID battery updates must use a 1 percent notify threshold."
}

if ($bleHid -notmatch '#define BLE_HID_BATTERY_SAMPLE_INTERVAL_MS 5000\b') {
    throw "BLE HID battery monitor must sample often enough to report live changes."
}

if ($bleHid -notmatch 'ble_hid_battery_level_exceeds_notify_threshold') {
    throw "BLE HID must compare the current battery level against the last notified level."
}

if ($bleHid -notmatch 'esp_hidd_dev_battery_set\(s_ble_hid_ctx\.hid_device, level\)') {
    throw "BLE HID must update the HID Battery Service from the sampled battery level."
}

if ($bleHid -notmatch 'ble_hid_update_battery_level\("connect_restore", true\)') {
    throw "BLE HID must force a battery service refresh on reconnect."
}

if ($bleHid -notmatch 'firmware_ota_note_battery\(') {
    throw "BLE HID battery updates must keep firmware OTA battery state current."
}

if ($bleHid -notmatch 'DIAG_BLE_BATTERY_LEVEL[\s\S]*battery\.raw_adc[\s\S]*battery\.adc_mv') {
    throw "BLE HID battery diagnostics must include raw ADC and ADC mV values."
}

if ($diagEvents -notmatch 'DIAG_BLE_BATTERY_LEVEL\s+5\s+/\*\s*a1=level, a2=voltage_mv, a3=raw_adc, a4=adc_mv\s+\*/') {
    throw "diag_log events must define DIAG_BLE_BATTERY_LEVEL with level, voltage, raw ADC, and ADC mV args."
}

Write-Host "PASS: battery monitor static checks passed for voltage mapping and live BLE HID battery reporting."
