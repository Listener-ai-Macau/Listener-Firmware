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
Assert-Contains $bleHid 'charger_active\s*=\s*power\.bat_chg_level\s*==\s*0' 'charging raw pin is active-low'
Assert-Contains $bleHid 'charge_power_present\s*=\s*usb_power_present\s*\|\|\s*charger_active' 'raw charge power uses USB or active charger'
Assert-Contains $bleHid 'raw_charging\s*=\s*charger_active' 'BLE charging status is not gated by GPIO7 digital level'
Assert-Contains $bleHid 'raw_full\s*=\s*power\.bat_std_level\s*==\s*0' 'charge-full raw pin is active-low and sampled independently of GPIO7'
Assert-Contains $bleHid 'charge_status_present\s*=\s*charge_power_present\s*\|\|\s*raw_full' 'BLE full estimator lets BAT_STD prove charger status when GPIO7 is low'
Assert-Contains $bleHid 'ble_hid_battery_estimate_charge_full\([\s\S]*charge_power_present[\s\S]*raw_charging[\s\S]*raw_full[\s\S]*now_ms' 'charge-full uses the debounced charger-status estimator'
Assert-Contains $bleHid 'charge_power_present\s*=\s*charge_power_present\s*\|\|\s*charge_full' 'BLE charge power reports debounced full status'
Assert-Contains $bleHid 'BLE_HID_BATTERY_CHARGE_FULL_DEBOUNCE_MS\s+10000U' 'charge-full debounce window'
Assert-Contains $bleHid 'if\s*\(charge_full\)\s*\{\s*level\s*=\s*100;\s*\}' 'BLE battery level reports 100 when charger full is asserted'
Assert-Contains $bleHid 'delta\s*>=\s*BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT' 'one-percent battery changes are notified'
Assert-Contains $bleHid 'BLE_HID_BATTERY_FORCE_REFRESH_INTERVAL_MS\s+60000' 'periodic battery refresh interval'
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
Assert-Contains $batteryMonitor 'BATTERY_MONITOR_EMPTY_MV\s+2800U' 'product-empty battery voltage'
Assert-Contains $batteryMonitor 'BATTERY_MONITOR_FULL_MV\s+4200U' 'full battery voltage'
Assert-Contains $featureMap 'Battery ADC status uses the protected product range `2800mV=0%` and `4200mV=100%`' 'feature map documents protected battery range'
Assert-Contains $featureMap 'forces a final pre-disconnect battery refresh before hardware shutdown' 'feature map documents final battery refresh before shutdown'

Write-Host "PASS: BLE HID Battery Service uses live ADC, charger full status, 1% notifications, and periodic refresh instead of a fixed placeholder."
