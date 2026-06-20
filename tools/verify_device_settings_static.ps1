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
$keyboard = Read-RepoFile "components/keyboard/keyboard.c"
$keyboardCmake = Read-RepoFile "components/keyboard/CMakeLists.txt"
$main = Read-RepoFile "main/main.c"
$mainCmake = Read-RepoFile "main/CMakeLists.txt"
$statusDoc = Read-RepoFile "docs/features/status_led.md"
$powerDoc = Read-RepoFile "docs/features/low_power_wake_policy.md"
$featureMap = Read-RepoFile "docs/features/firmware-feature-map.md"
$repoFeatures = Read-RepoFile "tools/ai/repo_features.ps1"

Assert-Contains $deviceHeader 'device_settings_snapshot_t' 'public snapshot type'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_BLE_NAME\s+"listener"' 'default BLE name is product default listener'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT\s+80U' 'default plugged brightness is 80 percent'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT\s+50U' 'default battery brightness is 50 percent'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT\s+100U' 'default per-zone LED brightness is 100 percent'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS\s+60000U' 'default low-power idle is one minute'
Assert-Contains $deviceHeader 'DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED\s+1' 'default plugged low-power remains enabled'
Assert-Contains $deviceHeader 'plugged_brightness_percent' 'plugged brightness field'
Assert-Contains $deviceHeader 'battery_brightness_percent' 'battery brightness field'
Assert-Contains $deviceHeader 'status_led_brightness_percent' 'status LED zone brightness field'
Assert-Contains $deviceHeader 'key_led_brightness_percent' 'key LED zone brightness field'
Assert-Contains $deviceHeader 'ec11_led_brightness_percent' 'EC11 LED zone brightness field'
Assert-Contains $deviceHeader 'edge_led_brightness_percent' 'edge LED zone brightness field'
Assert-Contains $deviceHeader 'low_power_idle_ms' 'low-power idle timeout field'
Assert-Contains $deviceHeader 'plugged_low_power_idle_ms' 'plugged low-power idle timeout field'
Assert-Contains $deviceHeader 'battery_low_power_idle_ms' 'battery low-power idle timeout field'
Assert-Contains $deviceHeader 'plugged_low_power_enabled' 'plugged low-power enable field'
Assert-Contains $deviceHeader 'plugged_auto_shutdown_ms' 'plugged shutdown timeout field'
Assert-Contains $deviceHeader 'battery_auto_shutdown_ms' 'battery-only shutdown timeout field'
Assert-Contains $deviceHeader 'device_settings_get_low_power_idle_ms' 'low-power idle getter'
Assert-Contains $deviceHeader 'device_settings_get_active_low_power_idle_ms' 'active low-power idle getter'
Assert-Contains $deviceHeader 'device_settings_get_plugged_low_power_enabled' 'plugged low-power getter'
Assert-Contains $deviceHeader 'device_settings_get_active_auto_shutdown_ms' 'active shutdown timeout getter'
Assert-Contains $deviceHeader 'device_settings_get_ble_name' 'BLE name getter'
Assert-Contains $deviceHeader 'device_settings_consume_control_command' 'return-code control command consumer'
Assert-Contains $deviceHeader 'device_settings_consume_usb_command' 'USB command consumer'

Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_NAMESPACE\s+"device"' 'device settings NVS namespace'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_PLUGGED_BRIGHTNESS_KEY' 'plugged brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_BATTERY_BRIGHTNESS_KEY' 'battery brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_STATUS_LED_BRIGHTNESS_KEY' 'status LED zone brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_KEY_LED_BRIGHTNESS_KEY' 'key LED zone brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_EC11_LED_BRIGHTNESS_KEY' 'EC11 LED zone brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_EDGE_LED_BRIGHTNESS_KEY' 'edge LED zone brightness NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_LOW_POWER_IDLE_MS_KEY' 'low-power idle NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_IDLE_MS_KEY' 'plugged low-power idle NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_BATTERY_LOW_POWER_IDLE_MS_KEY' 'battery low-power idle NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_KEY' 'plugged low-power NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_AUTO_SHUTDOWN_MS_KEY' 'auto-shutdown NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_PLUGGED_AUTO_SHUTDOWN_MS_KEY' 'plugged auto-shutdown NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_BLE_NAME_KEY' 'BLE name NVS key'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_NVS_KNOB_ROTATION_KEY' 'knob rotation NVS key'
Assert-Contains $deviceSettings 'plugged_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT' 'plugged brightness defaults through shared macro'
Assert-Contains $deviceSettings 'battery_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT' 'battery brightness defaults through shared macro'
Assert-Contains $deviceSettings 'status_led_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT' 'status LED zone brightness defaults through shared macro'
Assert-Contains $deviceSettings 'key_led_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT' 'key LED zone brightness defaults through shared macro'
Assert-Contains $deviceSettings 'ec11_led_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT' 'EC11 LED zone brightness defaults through shared macro'
Assert-Contains $deviceSettings 'edge_led_brightness_percent\s*=\s*DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT' 'edge LED zone brightness defaults through shared macro'
Assert-Contains $deviceSettings 'low_power_idle_ms\s*=\s*DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS' 'low-power idle defaults through shared macro'
Assert-Contains $deviceSettings 'plugged_low_power_enabled\s*=\s*DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED\s*!=\s*0' 'plugged low-power defaults through shared macro'
Assert-Contains $deviceSettings 'DEVICE_SETTINGS_USB_PREFIX\s+"DEVICE:"' 'DEVICE serial command prefix'
Assert-Contains $deviceSettings '~DEVICE:SETTINGS schema=listener\.device_settings\.v1' 'observable settings status line'
Assert-Contains $deviceSettings 'low_power_idle_mode=power_mode' 'power-mode low-power idle timeout wording'
Assert-Contains $deviceSettings 'plugged_low_power_idle_ms=%' 'observable plugged low-power timeout'
Assert-Contains $deviceSettings 'battery_low_power_idle_ms=%' 'observable battery low-power timeout'
Assert-Contains $deviceSettings 'plugged_low_power_enabled=%u' 'observable plugged low-power setting'
Assert-Contains $deviceSettings 'led_status=%u led_key=%u led_ec11=%u led_edge=%u' 'observable per-zone LED brightness settings'
Assert-Contains $deviceSettings 'auto_shutdown_enabled=%u' 'observable auto-shutdown enable state'
Assert-Contains $deviceSettings 'auto_shutdown_mode=%s' 'battery-only or disabled timeout wording'
Assert-Contains $deviceSettings 'plugged_auto_shutdown_ms=%' 'observable plugged shutdown timeout'
Assert-Contains $deviceSettings 'battery_auto_shutdown_ms=%' 'observable battery shutdown timeout'
Assert-Contains $deviceSettings 'auto_shutdown_ms_0_off_or_' 'auto-shutdown can be disabled for manual hardware shutdown tests'
Assert-Contains $deviceSettings 'device_settings_auto_shutdown_disabled_value' 'auto-shutdown disable aliases are parsed centrally'
Assert-Contains $deviceSettings 'device_settings_ascii_iequals' 'serial setting aliases tolerate operator casing'
Assert-Contains $deviceSettings 'knob_rotation=%s' 'observable knob rotation setting id'
Assert-Contains $deviceSettings 'ble_name_pending' 'BLE rename pending status'
Assert-Contains $deviceSettings 'brightness_must_be_0_100' 'brightness validation error'
Assert-Contains $deviceSettings 'led_zone_brightness_must_be_0_100' 'per-zone LED brightness validation error'
Assert-Contains $deviceSettings 'led_zone_brightness_0_100' 'per-zone LED brightness valid range'
Assert-Contains $deviceSettings 'low_power_idle_ms_out_of_range' 'low-power timeout validation error'
Assert-Contains $deviceSettings 'low_power_idle_minutes_out_of_range' 'low-power minutes validation error'
Assert-Contains $deviceSettings 'plugged_low_power_idle_minutes_out_of_range' 'plugged low-power minutes validation error'
Assert-Contains $deviceSettings 'battery_low_power_idle_minutes_out_of_range' 'battery low-power minutes validation error'
Assert-Contains $deviceSettings 'plugged_low_power_enabled_must_be_0_or_1' 'plugged low-power validation error'
Assert-Contains $deviceSettings 'auto_shutdown_ms_out_of_range' 'shutdown timeout validation error'
Assert-Contains $deviceSettings 'plugged_auto_shutdown_minutes_out_of_range' 'plugged shutdown minutes validation error'
Assert-Contains $deviceSettings 'ble_name_ascii_1_32' 'BLE name validation error'
Assert-Contains $deviceSettings 'knob_rotation_must_be_system_volume_screen_brightness_disabled' 'knob rotation validation error'
Assert-Contains $deviceSettings 'command_too_long' 'DEVICE SET rejects commands that would be truncated'
Assert-Contains $deviceSettings 'return\s+ret\s+!=\s+ESP_OK\s+\?\s+ret\s+:\s+ESP_ERR_INVALID_ARG' 'control command returns real validation failures'
Assert-Contains $deviceCmake 'REQUIRES\s+board\s+ec11_rotation_control\s+nvs_flash' 'device settings CMake dependencies'

Assert-Contains $statusLed '#include "device_settings\.h"' 'status LED includes device settings'
Assert-Contains $statusLed 'const uint8_t active_brightness = external_power_present' 'status LED computes active power-source brightness from snapshot'
Assert-Contains $statusLed '\? settings->plugged_brightness_percent\s*:\s*settings->battery_brightness_percent' 'status LED selects plugged or battery brightness from snapshot'
Assert-Contains $statusLed 'status_led_apply_device_settings_snapshot_locked' 'status LED applies the full device settings snapshot'
Assert-Contains $statusLed 'status_led_apply_zone_brightness_caps_locked' 'status LED applies per-zone brightness caps'
Assert-Contains $statusLed 'plugged_brightness_percent=%u battery_brightness_percent=%u active_power_brightness_percent=%u' 'LED status reports two brightness profiles'
Assert-Contains $statusLed 'status_zone_brightness_percent=%u key_zone_brightness_percent=%u' 'LED status reports status/key per-zone brightness caps'
Assert-Contains $statusLed 'ec11_zone_brightness_percent=%u edge_zone_brightness_percent=%u' 'LED status reports EC11/edge per-zone brightness caps'
Assert-Contains $statusLed 'user_brightness_is_hard_cap=1' 'routine product LEDs expose user brightness as hard cap'
Assert-Contains $statusLed 'STATUS_LED_STANDARD_PROFILE_CAP_PERCENT 100U' 'standard profile has no hidden percent cap above user brightness'
Assert-Contains $statusLed 'STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT 100U' 'ambient profile has no hidden percent cap above user brightness'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_PERIOD_MS 3600U' 'plugged charging breath uses a mature but responsive product cadence'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_LOW_HOLD_MS 450U' 'plugged charging breath rests briefly at a low point'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_RISE_MS 1300U' 'plugged charging breath rises smoothly without feeling stalled'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_HIGH_HOLD_MS 300U' 'plugged charging breath holds a readable peak without flashing'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_MIN_PERCENT 8U' 'plugged charging breath keeps a visible floor'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_BREATH_MAX_PERCENT 38U' 'plugged charging breath has enough range to be seen'
Assert-Contains $statusLed 'STATUS_LED_CHARGING_ACTIVE_WORK_MIN_PERCENT 28U' 'plugged charging PWR stays readable without overpowering recording or processing'
Assert-Contains $statusLed 'STATUS_LED_LOW_BATTERY_STEADY_PERCENT 24U' 'low battery uses a steady red cue instead of an unnecessary breathing effect'
Assert-Contains $statusLed 'status_led_charging_breath_percent\(now_ms\)' 'plugged charging breath uses the dedicated natural curve'
Assert-Contains $statusLed 'status_led_smoothstep_per_mille' 'plugged charging breath uses smoothstep easing'
Assert-Contains $statusLed 'STATUS_LED_FULL_STEADY_PERCENT STATUS_LED_PWR_WHITE_VISUAL_BALANCE_PERCENT' 'charge-full steady white is visually balanced and still user-capped'
Assert-Contains $statusLed 'device_settings_set_brightness_profiles\(brightness,\s*brightness\)' 'legacy LED brightness writes both profiles'
Assert-NotContains $statusLed 'status_led_apply_device_settings[\s\S]*?s_state\.output_disabled\s*=\s*false;[\s\S]*?status_led_set_last_reason_locked\("device_settings"\)' 'device settings brightness apply must not silently re-enable LEDs disabled by user or low-power state'
Assert-Contains $statusLedHeader 'status_led_apply_device_settings' 'status LED runtime apply API'
Assert-Contains $statusLedCmake 'device_settings' 'status LED CMake dependency'

Assert-Contains $powerManager '#include "device_settings\.h"' 'power manager includes device settings'
Assert-Contains $powerManager 'power_manager_hardware_shutdown_ms' 'power manager dynamic timeout helper'
Assert-Contains $powerManager 'power_manager_low_power_idle_ms' 'power manager dynamic low-power timeout helper'
Assert-Contains $powerManager 'power_manager_plugged_low_power_enabled' 'power manager plugged low-power helper'
Assert-Contains $powerManager 'device_settings_get_active_low_power_idle_ms\(s_external_power_present\)' 'power manager reads active configured low-power timeout'
Assert-Contains $powerManager 'device_settings_get_plugged_low_power_enabled\(\)' 'power manager reads configured plugged low-power switch'
Assert-Contains $powerManager 'device_settings_get_active_auto_shutdown_ms\(s_external_power_present\)' 'power manager reads active configured timeout'
Assert-Contains $powerManager 'hardware_shutdown_ms > 0U' 'automatic shutdown ignores disabled zero timeout'
Assert-Contains $powerManager 'TEST:SHUTDOWN' 'manual serial shutdown test alias'
Assert-Contains $powerManager 'low_power_idle_threshold_ms\s*=\s*power_manager_low_power_idle_ms\(\)' 'POWER:STATUS reports effective low-power timeout'
Assert-Contains $powerManager 's_external_power_present\s*&&\s*!power_manager_plugged_low_power_enabled\(\)[\s\S]*POWER_MANAGER_STATE_ACTIVE' 'plugged low-power switch blocks runtime idle while externally powered'
Assert-Contains $powerManager 'low_power_idle_allowed' 'POWER:STATUS reports effective low-power idle allowance'
Assert-Contains $powerManager 'hardware_shutdown_threshold_ms\s*=\s*power_manager_hardware_shutdown_ms\(\)' 'POWER:STATUS reports effective timeout'
Assert-Contains $powerManager 'power_manager_guard_runtime_power_hold_low' 'runtime PWR_HOLD low guard'
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

Assert-Contains $keyboard '#include "device_settings\.h"' 'keyboard BLE control includes device settings'
Assert-Contains $keyboard 'device_settings_consume_control_command\(command\)' 'keyboard BLE control dispatches DEVICE commands with return code'
Assert-Contains $keyboard 'status_led_apply_device_settings\(\)' 'keyboard BLE control applies DEVICE brightness updates'
Assert-Contains $keyboardCmake 'device_settings' 'keyboard CMake dependency for DEVICE BLE commands'

Assert-Contains $main '#include "device_settings\.h"' 'main includes device settings'
Assert-Contains $main 'device_settings_init\(\)' 'main initializes device settings after NVS POST'
Assert-Contains $main 'status_led_apply_device_settings\(\)' 'main reapplies LED brightness after loading settings'
Assert-Contains $mainCmake 'device_settings' 'main CMake dependency'

Assert-Contains $statusDoc '~DEVICE:SETTINGS' 'status LED docs name DEVICE settings'
Assert-Contains $statusDoc '~DEVICE:SET led_status=<0-100>' 'status LED docs name status zone brightness setting'
Assert-Contains $statusDoc '~DEVICE:SET led_key=<0-100>' 'status LED docs name key zone brightness setting'
Assert-Contains $statusDoc '~DEVICE:SET led_ec11=<0-100>' 'status LED docs name EC11 zone brightness setting'
Assert-Contains $statusDoc '~DEVICE:SET led_edge=<0-100>' 'status LED docs name edge zone brightness setting'
Assert-Contains $statusDoc '~DEVICE:SET low_power_idle_minutes' 'status LED docs name low-power setting'
Assert-Contains $powerDoc '~DEVICE:SET low_power_idle_ms' 'low-power docs name configurable idle timeout'
Assert-Contains $powerDoc '~DEVICE:SET plugged_low_power_enabled' 'low-power docs name plugged low-power switch'
Assert-Contains $powerDoc '~DEVICE:SET auto_shutdown_ms' 'low-power docs name configurable timeout'
Assert-Contains $featureMap 'components/device_settings/' 'feature map includes device settings'
Assert-Contains $repoFeatures 'verify_device_settings_static.ps1' 'repo feature script includes device settings verifier'

Write-Host "PASS: device settings static checks cover firmware command contract, persisted settings, status LED brightness profiles, per-zone LED brightness caps, split low-power idle timeouts, split auto-shutdown timeouts, BLE name source, CMake dependencies, and docs."
