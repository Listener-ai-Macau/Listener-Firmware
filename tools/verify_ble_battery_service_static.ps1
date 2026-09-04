param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

function Read-RepoFile {
    param([string]$RelativePath)
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Description
    )
    if ($Text -notmatch $Pattern) {
        throw "Missing $Description ($Pattern)"
    }
}

$bleHid = Read-RepoFile "ports/esp32/ble_hid/ble_hid.c"
$bleHidHeader = Read-RepoFile "ports/esp32/ble_hid/include/ble_hid.h"
$batteryMonitor = Read-RepoFile "components/battery_monitor/battery_monitor.c"
$powerManager = Read-RepoFile "components/power_manager/power_manager.c"
$featureMap = Read-RepoFile "docs/features/firmware-feature-map.md"

Assert-Contains $bleHid 'battery_monitor_read\(&battery\)' 'BLE Battery Service reads live battery ADC status'
Assert-Contains $bleHid 'board_get_v2_power_input_snapshot\(&power\)' 'BLE Battery Service reads charger full/USB power pins'
Assert-Contains $bleHid 'usb_power_present\s*=\s*power\.usb_power_present' 'USB power interpretation uses the board disabled USB_Det state'
Assert-Contains $bleHid 'board_decode_charger_status_pins\(power\.bat_chg_level,\s*power\.bat_std_level\)' 'BLE charger pins use exclusive charging vs full'
Assert-Contains $bleHid 'charger_active\s*=\s*charger\.charging' 'charging raw pin is exclusive of full'
Assert-Contains $bleHid 'charge_power_present\s*=\s*usb_power_present\s*\|\|\s*charger_active\s*\|\|\s*charger\.standby_full' 'raw charge power uses USB, exclusive charging, or exclusive full'
Assert-Contains $bleHid 'raw_charging\s*=\s*charger_active' 'BLE charging status is not gated by GPIO7 digital level'
Assert-Contains $bleHid 'raw_full\s*=\s*charger\.standby_full' 'charge-full raw pin is exclusive of charging'
Assert-Contains $bleHid 'denzic_battery_v1_update_charge\(&s_battery_charge_tracker,\s*&charge_input\)' 'charge-full uses the shared debounced charger-status estimator'
Assert-Contains $bleHid 'charge_full = charge.state == DENZIC_BATTERY_V1_CHARGE_STATE_FULL' 'BLE full follows exclusive charger VIN plus debounce'
Assert-Contains $bleHid 'charge_power_present = charge.charge_power_present' 'BLE charge power reports debounced full status'
Assert-Contains $bleHid 'level = charge.published_level' 'BLE battery level reports 100 when charger full is asserted'
Assert-Contains $bleHid 'denzic_battery_v1_decide_notify\(&notify_input\)' 'one-percent battery changes are notified'
Assert-Contains $bleHid 'BLE_HID_BATTERY_FORCE_REFRESH_INTERVAL_MS\s+DENZIC_BATTERY_V1_FORCE_REFRESH_INTERVAL_MS' 'periodic battery refresh interval'
Assert-Contains $bleHid 'BLE_HID_BATTERY_CONNECTED_IDLE_INTERVAL_MS\s+60000' 'connected idle battery refresh interval for host battery displays'
Assert-Contains $bleHid 'BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS\s+600000' 'disconnected idle battery refresh remains infrequent'
Assert-Contains $bleHid 'low_power_idle\s*\?\s*BLE_HID_BATTERY_CONNECTED_IDLE_INTERVAL_MS\s*:\s*BLE_HID_BATTERY_SAMPLE_INTERVAL_MS' 'connected idle uses the shorter host battery refresh interval'
Assert-Contains $bleHid '"connected_idle_sample"' 'connected idle battery refresh log reason'
Assert-Contains $bleHid 'periodic_refresh' 'periodic battery notification refresh'
Assert-Contains $bleHid 'esp_hidd_dev_battery_set\(s_ble_hid_ctx\.hid_device,\s*level\)' 'HID Battery Service value is updated through ESP HID'
Assert-Contains $bleHid 'firmware_ota_note_battery\(' 'OTA battery gate receives the same live battery sample'
Assert-Contains $bleHidHeader 'esp_err_t\s+ble_hid_battery_force_refresh\(const char \*reason\)' 'public forced battery refresh API'
Assert-Contains $bleHid 'esp_err_t\s+ble_hid_battery_force_refresh\(const char \*reason\)' 'forced battery refresh implementation'
Assert-Contains $powerManager 'ble_hid_battery_force_refresh\("pre_shutdown"\)' 'hardware shutdown forces a final battery refresh before BLE disconnect'
Assert-Contains $powerManager 'POWER_MANAGER_SHUTDOWN_BATTERY_NOTIFY_WAIT_MS\s+100U' 'shutdown allows the final battery notification to leave before disconnect'
$preShutdownBatteryRefreshIndex = $powerManager.IndexOf('ble_hid_battery_force_refresh("pre_shutdown")')
$shutdownBleDisconnectIndex = $powerManager.IndexOf('esp_err_t ble_ret = ble_hid_gap_prepare_shutdown_disconnect()')
if ($preShutdownBatteryRefreshIndex -lt 0 -or
    $shutdownBleDisconnectIndex -lt 0 -or
    $preShutdownBatteryRefreshIndex -gt $shutdownBleDisconnectIndex) {
    throw "Hardware shutdown battery refresh must run before BLE disconnect preparation"
}
Assert-Contains $batteryMonitor 'BATTERY_MONITOR_EMPTY_MV\s+2850U' 'product-empty battery voltage'
Assert-Contains $batteryMonitor 'BATTERY_MONITOR_FULL_MV\s+4150U' 'full battery voltage'
Assert-Contains $batteryMonitor 'BATTERY_MONITOR_NVS_ADC_TRIM_KEY\s+"adc_trim_mv"' 'battery ADC DMM trim NVS key'
Assert-Contains $batteryMonitor 'battery_monitor_calibrate_adc_trim_from_dmm_mv' 'battery ADC DMM trim calibration API'
Assert-Contains $batteryMonitor 'adc_raw_mv' 'battery monitor exposes raw ADC pad millivolts'
Assert-Contains $batteryMonitor 'adc_driver_mv' 'battery monitor exposes driver ADC pad millivolts'
Assert-Contains $batteryMonitor 'adc_trim_mv' 'battery monitor exposes DMM trim millivolts'
Assert-Contains $featureMap 'Battery ADC status uses the protected product range `2850mV=0%` and `4150mV=100%`' 'feature map documents protected battery range'
Assert-Contains $featureMap 'NVS-persisted DMM trim' 'feature map documents battery ADC DMM trim'
Assert-Contains $featureMap 'forces a final pre-disconnect battery refresh before hardware shutdown' 'feature map documents final battery refresh before shutdown'

Write-Host "PASS: BLE HID Battery Service uses live ADC, charger full status, 1% notifications, and periodic refresh instead of a fixed placeholder."
