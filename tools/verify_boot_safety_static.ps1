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

$bootSafety = Read-RepoFile "components/boot_safety/boot_safety.c"
Assert-Contains $bootSafety 'RTC_NOINIT_ATTR static boot_safety_rtc_state_t s_rtc_state' 'RTC noinit crash counter state'
Assert-Contains $bootSafety 'BOOT_SAFETY_SAFE_MODE_THRESHOLD 3u' 'three-crash safe mode threshold'
Assert-Contains $bootSafety 'BOOT_SAFETY_NORMAL_CLEAR_DELAY_MS 30000u' '30 second normal boot clear timer'
Assert-Contains $bootSafety 'ESP_RST_POWERON' 'power-on reset does not count as crash'
Assert-Contains $bootSafety 'ESP_RST_DEEPSLEEP' 'deep sleep reset does not count as crash'
Assert-Contains $bootSafety 'crash_count \+= 1|s_rtc_state\.crash_count\+\+' 'crash counter increment'
Assert-Contains $bootSafety 's_rtc_state\.safe_mode_latched = 1' 'safe mode latch'
Assert-Contains $bootSafety 'esp_restart\(\)' 'USB crash validation restart command'
Assert-Contains $bootSafety 'BOOT_SAFETY_USB_PREFIX "BOOT:"' 'BOOT USB command prefix'

$bootSafetyHeader = Read-RepoFile "components/boot_safety/include/boot_safety.h"
Assert-Contains $bootSafetyHeader 'boot_safety_is_safe_mode' 'safe mode query API'
Assert-Contains $bootSafetyHeader 'boot_safety_consume_usb_command' 'safe mode USB command API'

$main = Read-RepoFile "main/main.c"
Assert-Contains $main 'boot_safety_init\(\)' 'boot safety init in app_main'
Assert-Contains $main 'boot_safety_is_safe_mode\(\)' 'safe mode branch in app_main'
Assert-Contains $main 'keyboard_start_safe_mode\(\)' 'safe mode keyboard path'
Assert-Contains $main 'boot_safety_start_normal_boot_clear_timer\(\)' 'normal boot clear timer start'
Assert-Contains $main 'device_status state=recovery detail=boot_safety_safe_mode' 'safe mode recovery status log'

$keyboard = Read-RepoFile "components/keyboard/keyboard.c"
Assert-Contains $keyboard 'keyboard_start_safe_mode' 'keyboard safe mode entry point'
Assert-Contains $keyboard 'voice recording control and audio capture are disabled' 'safe mode disables voice recording'

$bleHid = Read-RepoFile "ports/esp32/ble_hid/ble_hid.c"
Assert-Contains $bleHid 'boot_safety_consume_usb_command\(line\)' 'BOOT USB command dispatch'
Assert-Contains $bleHid 'ble_hid_set_safe_mode' 'BLE safe mode setter'
Assert-Contains $bleHid 'safe mode: BLE audio GATT disabled' 'safe mode skips BLE audio GATT'
Assert-Contains $bleHid 'ble_hid_gap_set_audio_enabled\(!s_safe_mode\)' 'safe mode disables BLE audio GAP integration'

$bleGap = Read-RepoFile "ports/esp32/ble_hid_gap/ble_hid_gap_esp32.c"
Assert-Contains $bleGap 'ble_hid_gap_set_audio_enabled' 'BLE GAP audio integration switch'
Assert-Contains $bleGap 'if \(s_audio_enabled\)\s*\{\s*ble_audio_stream_on_gap_connect' 'safe mode guards BLE audio connect callback'
Assert-Contains $bleGap 'if \(s_audio_enabled\)\s*\{\s*ble_audio_stream_on_gap_mtu' 'safe mode guards BLE audio MTU callback'
Assert-Contains $bleGap 'if \(s_audio_enabled\)\s*\{\s*ble_audio_stream_on_gap_notify_tx' 'safe mode guards BLE audio notify callback'

$listenerDevice = Read-RepoFile "protocols/listener_device/listener_device.c"
Assert-Contains $listenerDevice 'boot_safety_safe_mode;audio_disabled' 'safe mode BLE capabilities'
Assert-Contains $listenerDevice 'listener_device_set_safe_mode' 'listener device safe mode setter'

$diagEvents = Read-RepoFile "components/diag_log/include/diag_log_events.h"
Assert-Contains $diagEvents 'DIAG_SYS_BOOT_SAFETY' 'diag log boot safety event'

Write-Host "PASS: boot safety static checks passed."
