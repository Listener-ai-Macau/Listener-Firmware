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
$boardPath = Join-Path $RepoRoot "components\board\board.c"
$diagEventsPath = Join-Path $RepoRoot "components\diag_log\include\diag_log_events.h"
if (-not (Test-Path -LiteralPath $bleHidPath)) {
    throw "Missing BLE HID source: $bleHidPath"
}
if (-not (Test-Path -LiteralPath $boardPath)) {
    throw "Missing board source: $boardPath"
}
if (-not (Test-Path -LiteralPath $diagEventsPath)) {
    throw "Missing diag log events header: $diagEventsPath"
}
$bleHid = Get-Content -LiteralPath $bleHidPath -Raw
$board = Get-Content -LiteralPath $boardPath -Raw
$diagEvents = Get-Content -LiteralPath $diagEventsPath -Raw

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_ABSOLUTE_MIN_MV 2700U\r?$') {
    throw "Battery absolute minimum marker must remain 2700mV."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_EMPTY_MV 2850U\r?$') {
    throw "Battery product empty voltage must be 2850mV."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_FULL_MV 4150U\r?$') {
    throw "Battery full voltage must be 4150mV."
}

if ($text -match 'BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE|source_impedance') {
    throw "Battery ADC must not use a fixed source-impedance multiplier."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_ADC_DMM_TRIM_MIN_MV \(-300\)\r?$' -or
    $text -notmatch '(?m)^#define BATTERY_MONITOR_ADC_DMM_TRIM_MAX_MV 300\r?$') {
    throw "Battery ADC DMM trim must be bounded to a plausible board-level offset."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_NVS_NAMESPACE "battery"\r?$' -or
    $text -notmatch '(?m)^#define BATTERY_MONITOR_NVS_ADC_TRIM_KEY "adc_trim_mv"\r?$') {
    throw "Battery ADC DMM trim must use the expected NVS namespace/key."
}

if ($text -notmatch '(?m)^#define BATTERY_MONITOR_ADC_DISCARD_COUNT [1-9][0-9]*U\r?$') {
    throw "ADC channel switching must discard at least one sample before reporting battery/current telemetry readings."
}

if ($text -notmatch 'esp_rom_delay_us\(BATTERY_MONITOR_ADC_SETTLE_US\)[\s\S]*adc_oneshot_read\(s_adc1_handle,\s*state->channel,\s*&discard_raw\)[\s\S]*esp_rom_delay_us\(BATTERY_MONITOR_ADC_SETTLE_US\)[\s\S]*for\s*\(uint8_t i = 0; i < BATTERY_MONITOR_SAMPLE_COUNT') {
    throw "ADC reads must allow mux settle and discard stale samples before averaging reported samples."
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

if ($text -notmatch 'battery_monitor_load_adc_trim_locked[\s\S]*nvs_get_i32[\s\S]*BATTERY_MONITOR_NVS_ADC_TRIM_KEY[\s\S]*s_adc_trim_mv') {
    throw "Battery ADC DMM trim must load the persisted trim from NVS."
}

if ($text -notmatch 'battery_monitor_store_adc_trim_locked[\s\S]*nvs_set_i32[\s\S]*nvs_commit') {
    throw "Battery ADC DMM trim must be persisted through NVS commit."
}

if ($text -notmatch 'battery_monitor_calibrate_adc_trim_from_dmm_mv[\s\S]*dmm_pad_mv[\s\S]*measured_pad_mv[\s\S]*battery_monitor_store_adc_trim_locked') {
    throw "Battery ADC DMM trim calibration must derive trim from the current ADC sample and DMM pad mV."
}

if ($text -notmatch 'battery_monitor_apply_adc_trim[\s\S]*driver_pad_mv[\s\S]*trim_mv' -or
    $text -notmatch 'battery_monitor_battery_mv_from_pad_mv\(calibrated_pad_mv\)') {
    throw "Battery ADC pad voltage must apply NVS DMM trim before VBAT reconstruction."
}

if ($text -notmatch '\.adc_raw_mv\s*=\s*driver_pad_mv[\s\S]*\.adc_driver_mv\s*=\s*driver_pad_mv[\s\S]*\.adc_mv\s*=\s*calibrated_pad_mv[\s\S]*\.adc_trim_mv\s*=\s*\(int\)trim_mv') {
    throw "Battery status must expose driver pad mV, final pad mV, and DMM trim mV."
}

if ($text -notmatch 'battery_monitor_store_power_rail_cache_locked[\s\S]*status->sequence\s*=\s*\+\+s_power_rail_sequence[\s\S]*\*cache\s*=\s*\*status') {
    throw "Power rail current telemetry must cache the latest forced ADC sample with a sequence number."
}

if ($text -notmatch 'bool\s+battery_monitor_get_cached_power_rail[\s\S]*battery_monitor_power_rail_cache[\s\S]*\*out_status\s*=\s*\*cache') {
    throw "Power rail current telemetry must expose cached samples for passive idle diagnostics."
}

if ($bleHid -notmatch '#define BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT 1\b') {
    throw "BLE HID battery updates must use a 1 percent notify threshold."
}

if ($bleHid -notmatch '#define BLE_HID_BATTERY_SAMPLE_INTERVAL_MS 5000\b') {
    throw "BLE HID battery monitor must sample often enough to report live changes."
}

if ($bleHid -notmatch '#define BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS 600000\b') {
    throw "BLE HID battery monitor must heavily back off while disconnected/idle."
}

if ($bleHid -notmatch 'ble_hid_low_power_idle_active[\s\S]*POWER_MANAGER_STATE_CONNECTED_IDLE[\s\S]*POWER_MANAGER_STATE_DISCONNECTED_IDLE') {
    throw "BLE HID battery monitor must detect connected and disconnected low-power idle states."
}

if ($bleHid -notmatch 's_ble_connected\s*&&\s*!low_power_idle[\s\S]*BLE_HID_BATTERY_SAMPLE_INTERVAL_MS[\s\S]*BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS') {
    throw "BLE HID battery monitor must use the active interval only while connected and not in low-power idle."
}

if ($bleHid -notmatch 's_ble_connected\s*&&\s*!low_power_idle[\s\S]*"threshold_sample"[\s\S]*"idle_sample"') {
    throw "BLE HID battery monitor must mark connected/disconnected low-power samples as idle_sample."
}

if ($bleHid -notmatch '!s_ble_connected\s*&&\s*!force_notify[\s\S]*battery update skipped while disconnected') {
    throw "BLE HID battery monitor must skip routine HID battery service updates while disconnected."
}

if ($bleHid -notmatch 's_ble_connected\s*&&\s*!low_power_idle[\s\S]*watchdog_platform_task_notify_take\([\s\S]*watchdog_platform_task_notify_take_low_power\(') {
    throw "BLE HID battery task must use a low-power long wait while disconnected or connected-idle and wake promptly on state changes."
}

if ($bleHid -notmatch 'notified\s*!=\s*0\s*&&\s*low_power_idle[\s\S]*continue;') {
    throw "BLE HID battery task must not take an immediate ADC sample just because low-power idle notified it."
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

if ($bleHid -notmatch 'adc_driver_mv=%d[\s\S]*adc_trim_mv=%d[\s\S]*adc_trim_result=%s[\s\S]*battery\.adc_driver_mv[\s\S]*battery\.adc_trim_mv[\s\S]*battery\.adc_trim_result') {
    throw "BLE HID battery logs must include driver ADC pad mV and DMM trim diagnostics."
}

if ($board -notmatch '~BATTERY:STATUS' -or
    $board -notmatch '~BATTERY:CAL:DMM <mV>' -or
    $board -notmatch 'battery_adc_driver_mv=%d' -or
    $board -notmatch 'battery_adc_trim_mv=%d' -or
    $board -notmatch 'adc_dmm_trim_nvs') {
    throw "Board diagnostics must expose BATTERY status/calibration commands and DMM trim fields."
}

if ($diagEvents -notmatch 'DIAG_BLE_BATTERY_LEVEL\s+5\s+/\*\s*a1=level, a2=voltage_mv, a3=raw_adc, a4=adc_mv\s+\*/') {
    throw "diag_log events must define DIAG_BLE_BATTERY_LEVEL with level, voltage, raw ADC, and ADC mV args."
}

Write-Host "PASS: battery monitor static checks passed for voltage mapping and live BLE HID battery reporting."
