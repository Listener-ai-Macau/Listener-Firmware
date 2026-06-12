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

$defaults = Read-RepoFile "sdkconfig.defaults"
$defaultsEsp32s3 = Read-RepoFile "sdkconfig.defaults.esp32s3"

foreach ($configText in @($defaults, $defaultsEsp32s3)) {
    Assert-Contains $configText '(?m)^CONFIG_ESP_TASK_WDT_EN=y\r?$' 'Task WDT enable'
    Assert-Contains $configText '(?m)^CONFIG_ESP_TASK_WDT_INIT=y\r?$' 'Task WDT init'
    Assert-Contains $configText '(?m)^CONFIG_ESP_TASK_WDT_PANIC=y\r?$' 'Task WDT panic'
    Assert-Contains $configText '(?m)^CONFIG_ESP_TASK_WDT_TIMEOUT_S=5\r?$' 'Task WDT 5 second timeout'
    Assert-Contains $configText '(?m)^CONFIG_ESP_INT_WDT=y\r?$' 'Interrupt WDT enable'
    Assert-Contains $configText '(?m)^CONFIG_ESP_INT_WDT_TIMEOUT_MS=300\r?$' 'Interrupt WDT 300ms timeout'
}

$requiredTaskFiles = @{
    "ports/esp32/ble_hid/ble_hid.c" = @(
        "watchdog_platform_subscribe_current_task(`"ble_hid_keyboard_task`")",
        "watchdog_platform_subscribe_current_task(`"ble_hid_battery_task`")"
    )
    "ports/esp32/ble_audio_stream/ble_audio_stream_esp32.c" = @(
        "watchdog_platform_subscribe_current_task(`"ble_audio_stream_task`")"
    )
    "ports/esp32/audio_capture/audio_capture_esp32.c" = @(
        "watchdog_platform_subscribe_current_task(`"audio_capture_task`")"
    )
    "components/keyboard/keyboard.c" = @(
        "watchdog_platform_subscribe_current_task(`"keyboard_custom_task`")"
    )
    "components/voice_recording_control/voice_recording_control.c" = @(
        "watchdog_platform_subscribe_current_task(`"voice_recording_control_task`")"
    )
    "ports/esp32/voice_key_input/voice_key_input_esp32.c" = @(
        "watchdog_platform_subscribe_current_task(`"voice_key_input_task`")"
    )
    "components/power_manager/power_manager.c" = @(
        "watchdog_platform_subscribe_current_task(`"power_manager_task`")"
    )
    "ports/esp32/system_health_platform/system_health_esp32.c" = @(
        "watchdog_platform_subscribe_current_task(`"health_task`")"
    )
}

foreach ($entry in $requiredTaskFiles.GetEnumerator()) {
    $text = Read-RepoFile $entry.Key
    foreach ($needle in $entry.Value) {
        if (-not $text.Contains($needle)) {
            throw "Missing watchdog task subscription '$needle' in $($entry.Key)"
        }
    }
    if (-not $text.Contains("watchdog_platform_feed_current_task") -and
        -not $text.Contains("watchdog_platform_delay_ms") -and
        -not $text.Contains("watchdog_platform_task_notify_take")) {
        throw "Missing watchdog feed path in $($entry.Key)"
    }
}

$watchdog = Read-RepoFile "ports/esp32/watchdog_platform/watchdog_platform_esp32.c"
Assert-Contains $watchdog 'esp_task_wdt_add\(NULL\)' 'current task subscription API'
Assert-Contains $watchdog 'esp_task_wdt_reset\(\)' 'current task feed API'
Assert-Contains $watchdog 'watchdog_platform_task_notify_take_low_power' 'low-power task wait API'
Assert-Contains $watchdog 'esp_task_wdt_delete\(NULL\)' 'low-power wait WDT unsubscribe'
Assert-Contains $watchdog 'WATCHDOG_PLATFORM_FEED_INTERVAL_MS 1000U' 'bounded long-wait feed interval'
Assert-Contains $watchdog 'WDT DEADLOCK test command accepted' 'watchdog deadlock validation command'

$watchdogHeader = Read-RepoFile "ports/esp32/watchdog_platform/include/watchdog_platform.h"
Assert-Contains $watchdogHeader 'watchdog_platform_consume_usb_command' 'watchdog USB command API'
Assert-Contains $watchdogHeader 'watchdog_platform_task_notify_take_low_power' 'watchdog low-power wait API'

$bleHid = Read-RepoFile "ports/esp32/ble_hid/ble_hid.c"
Assert-Contains $bleHid 'watchdog_platform_consume_usb_command\(line\)' 'watchdog USB command dispatch'
Assert-Contains $bleHid 'watchdog_platform_task_notify_take_low_power\([\s\S]*ble_hid_battery_sample_interval_ms\(\)' 'BLE battery low-power wait'

$powerManager = Read-RepoFile "components/power_manager/power_manager.c"
Assert-Contains $powerManager 'watchdog_platform_task_notify_take_low_power\([\s\S]*POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS' 'power manager low-power wait'

$health = Read-RepoFile "ports/esp32/system_health_platform/system_health_esp32.c"
Assert-Contains $health 'watchdog_platform_task_notify_take_low_power\(pdTRUE, interval_s \* 1000U\)' 'health low-power wait'

Write-Host "PASS: watchdog boot-safety static checks passed."
