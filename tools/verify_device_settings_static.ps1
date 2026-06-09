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
        [string]$Label
    )
    if ($Text -notmatch $Pattern) {
        throw "Missing $Label ($Pattern)"
    }
}

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Label
    )
    if ($Text -match $Pattern) {
        throw "Unexpected $Label ($Pattern)"
    }
}

$deviceHeader = Read-RepoFile "components/device_settings/include/device_settings.h"
$deviceSettings = Read-RepoFile "components/device_settings/device_settings.c"
$deviceCmake = Read-RepoFile "components/device_settings/CMakeLists.txt"
$statusLed = Read-RepoFile "components/status_led/status_led.c"
$statusLedHeader = Read-RepoFile "components/status_led/include/status_led.h"
$statusLedCmake = Read-RepoFile "components/status_led/CMakeLists.txt"
$powerManager = Read-RepoFile "components/power_manager/power_manager.c"
$powerCmake = Read-RepoFile "components/power_manager/CMakeLists.txt"
$listenerDevice = Read-RepoFile "protocols/listener_device/listener_device.c"
$listenerDeviceCmake = Read-RepoFile "protocols/listener_device/CMakeLists.txt"
$bleHid = Read-RepoFile "ports/esp32/ble_hid/ble_hid.c"
$bleHidCmake = Read-RepoFile "ports/esp32/ble_hid/CMakeLists.txt"
$main = Read-RepoFile "main/main.c"
$mainCmake = Read-RepoFile "main/CMakeLists.txt"
$statusDoc = Read-RepoFile "docs/features/status_led.md"
$powerDoc = Read-RepoFile "docs/features/low_power_wake_policy.md"
$featureMap = Read-RepoFile "docs/features/firmware-feature-map.md"
$repoFeatures = Read-RepoFile "tools/ai/repo_features.ps1"

Assert-Contains $deviceHeader 'device_settings_snapshot_t' 'public snapshot type'
Assert-Contains $deviceHeader 'plugged_brightness_percent' 'plugged brightness field'
Assert-Contains $deviceHeader 'battery_brightness_percent' 'battery brightness field'
Assert-Contains $deviceHeader 'battery_auto_shutdown_ms' 'battery-only shutdown timeout field'
Assert-Contains $deviceHeader 'device_settings_get_ble_name' 'BLE name getter'
Assert-Contains $deviceHeader 'device_settings_consume_usb_command' 'USB command consumer'

Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_NAMESPACE\s+"device"' 'device settings NVS namespace'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_PLUGGED_BRIGHTNESS_KEY' 'plugged brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_BATTERY_BRIGHTNESS_KEY' 'battery brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_AUTO_SHUTDOWN_MS_KEY' 'auto-shutdown NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_BLE_NAME_KEY' 'BLE name NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_USB_PREFIX\s+"DEVICE:"' 'DEVICE serial command prefix'
Assert-Contains $deviceSettings '~DEVICE:SETTINGS schema=listener\.device_settings\.v1' 'observable settings status line'
Assert-Contains $deviceSettings 'auto_shutdown_mode=battery_only' 'battery-only timeout wording'
Assert-Contains $deviceSettings 'ble_name_pending' 'BLE rename pending status'
Assert-Contains $deviceSettings 'brightness_must_be_0_100' 'brightness validation error'
Assert-Contains $deviceSettings 'auto_shutdown_ms_out_of_range' 'shutdown timeout validation error'
Assert-Contains $deviceSettings 'ble_name_ascii_1_32' 'BLE name validation error'
Assert-Contains $deviceSettings 'command_too_long' 'DEVICE SET rejects commands that would be truncated'
Assert-Contains $deviceCmake 'REQUIRES\s+board\s+nvs_flash' 'device settings CMake dependencies'

Assert-Contains $statusLed '#include "device_settings\.h"' 'status LED includes device settings'
Assert-Contains $statusLed 'device_settings_get_active_brightness_percent\(external_power_present\)' 'status LED applies active power-source brightness'
Assert-Contains $statusLed 'plugged_brightness_percent=%u battery_brightness_percent=%u active_power_brightness_percent=%u' 'LED status reports two brightness profiles'
Assert-Contains $statusLed 'device_settings_set_brightness_profiles\(brightness,\s*brightness\)' 'legacy LED brightness writes both profiles'
Assert-NotContains $statusLed 'status_led_apply_device_settings[\s\S]*?s_state\.output_disabled\s*=\s*false;[\s\S]*?status_led_set_last_reason_locked\("device_settings"\)' 'device settings brightness apply must not silently re-enable LEDs disabled by user or low-power state'
Assert-Contains $statusLedHeader 'status_led_apply_device_settings' 'status LED runtime apply API'
Assert-Contains $statusLedCmake 'device_settings' 'status LED CMake dependency'

Assert-Contains $powerManager '#include "device_settings\.h"' 'power manager includes device settings'
Assert-Contains $powerManager 'power_manager_hardware_shutdown_ms' 'power manager dynamic timeout helper'
Assert-Contains $powerManager 'device_settings_get_battery_auto_shutdown_ms\(\)' 'power manager reads configured battery timeout'
Assert-Contains $powerManager 'hardware_shutdown_threshold_ms\s*=\s*power_manager_hardware_shutdown_ms\(\)' 'POWER:STATUS reports effective timeout'
Assert-Contains $powerCmake 'device_settings' 'power manager CMake dependency'

Assert-Contains $listenerDevice '#include "device_settings\.h"' 'listener_device includes device settings'
Assert-Contains $listenerDevice 'return\s+device_settings_get_ble_name\(\)' 'BLE name comes from device settings'
Assert-Contains $listenerDeviceCmake 'device_settings' 'listener_device CMake dependency'
Assert-Contains $bleHid '#include "device_settings\.h"' 'BLE HID includes device settings'
Assert-Contains $bleHid 'BLE_HID_USB_COMMAND_BUFFER_BYTES\s+192' 'USB command buffer fits combined DEVICE SET commands'
Assert-Contains $bleHid 'device_settings_consume_usb_command\(line\)' 'BLE HID dispatches DEVICE commands'
Assert-Contains $bleHid 'status_led_apply_device_settings\(\)' 'DEVICE command applies status LED settings'
Assert-Contains $bleHid 's_device_name\s*=\s*listener_device_get_ble_name\(\)' 'BLE HID uses configured BLE name at init'
Assert-Contains $bleHidCmake 'device_settings' 'BLE HID CMake dependency'

Assert-Contains $main '#include "device_settings\.h"' 'main includes device settings'
Assert-Contains $main 'device_settings_init\(\)' 'main initializes device settings after NVS POST'
Assert-Contains $main 'status_led_apply_device_settings\(\)' 'main reapplies LED brightness after loading settings'
Assert-Contains $mainCmake 'device_settings' 'main CMake dependency'

Assert-Contains $statusDoc '~DEVICE:SETTINGS' 'status LED docs name DEVICE settings'
Assert-Contains $powerDoc '~DEVICE:SET auto_shutdown_ms' 'low-power docs name configurable timeout'
Assert-Contains $featureMap 'components/device_settings/' 'feature map includes device settings'
Assert-Contains $repoFeatures 'verify_device_settings_static.ps1' 'repo feature script includes device settings verifier'

Write-Host "PASS: device settings static checks cover firmware command contract, persisted settings, status LED brightness profiles, battery-only timeout, BLE name source, CMake dependencies, and docs."
