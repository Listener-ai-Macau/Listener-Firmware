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
        throw "verify_ble_status_led_connected_sync failed: $Description"
    }
}

function Assert-Order {
    param(
        [string]$Text,
        [string]$Earlier,
        [string]$Later,
        [string]$Description
    )
    $earlierIndex = $Text.IndexOf($Earlier)
    $laterIndex = $Text.IndexOf($Later)
    if ($earlierIndex -lt 0 -or $laterIndex -lt 0 -or $earlierIndex -ge $laterIndex) {
        throw "verify_ble_status_led_connected_sync failed: $Description"
    }
}

$gap = Read-RepoFile "ports\esp32\ble_hid_gap\ble_hid_gap_esp32.c"
$hid = Read-RepoFile "ports\esp32\ble_hid\ble_hid.c"
$audio = Read-RepoFile "ports\esp32\ble_audio_stream\ble_audio_stream_esp32.c"
$audioCMake = Read-RepoFile "ports\esp32\ble_audio_stream\CMakeLists.txt"
$audioHeader = Read-RepoFile "ports\esp32\ble_audio_stream\include\ble_audio_stream.h"
$statusLed = Read-RepoFile "components\status_led\status_led.c"
$statusDoc = Read-RepoFile "docs\features\status_led.md"

$advStartIndex = $gap.IndexOf("esp_err_t esp_hid_ble_gap_adv_start(void)")
$advEndIndex = $gap.IndexOf("/*`n * CONTROLLER INIT", $advStartIndex)
if ($advEndIndex -lt 0) {
    $advEndIndex = $gap.IndexOf("/*`r`n * CONTROLLER INIT", $advStartIndex)
}
if ($advStartIndex -lt 0 -or $advEndIndex -lt 0 -or $advEndIndex -le $advStartIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate esp_hid_ble_gap_adv_start"
}
$advStart = $gap.Substring($advStartIndex, $advEndIndex - $advStartIndex)

Assert-Contains $gap 'ble_hid_gap_connection_snapshot_t\s+conn\s*=\s*ble_hid_gap_connection_snapshot\(\);' `
    "GAP advertising entry must read the shared connection snapshot"
Assert-Contains $gap 'if\s*\(\s*conn\.connected\s*\)\s*\{[\s\S]*?NimBLE advertising skipped: GAP already connected[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*false\);[\s\S]*?return ESP_OK;' `
    "GAP advertising must skip stale advertising while connected and restore connected LED"
Assert-Order $advStart 'if (conn.connected)' 'if (ble_gap_adv_active())' `
    "connected guard must run before advertising-active handling"
Assert-Order $advStart 'if (conn.connected)' 'status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);' `
    "connected guard must run before directed advertising can set reconnecting LED"
Assert-Order $advStart 'if (conn.connected)' 'status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);' `
    "connected guard must run before undirected advertising can set pairing LED"
Assert-Contains $gap 'ble_hid_gap_set_connection_state\(true,\s*event->connect\.conn_handle\);[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "GAP connect event must mark connected before refreshing connected LED"
Assert-Contains $gap 'ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\);[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "GAP disconnect event must clear connected before reconnecting LED"
Assert-Contains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*true;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "HID connect event must refresh connected LED and confidence window"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "HID disconnect event must still drive reconnecting LED"
Assert-Contains $hid 'static\s+status_led_ble_state_t\s+ble_hid_connected_status_led_state\([^)]*\)[\s\S]*?ble_audio_stream_is_type_link_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED' `
    "HID connected-status resync must preserve TYPE_READY while Listener-Type link is ready or streaming"
Assert-Contains $hid 'static\s+void\s+ble_hid_resync_connected_status_led\([^)]*\)[\s\S]*?if\s*\(\s*!s_ble_connected\s*\)[\s\S]*?status_led_set_ble_state\(ble_hid_connected_status_led_state\(\),\s*false\);' `
    "HID connected-status resync must restore the BLE LED without restarting the confidence animation"
Assert-Contains $hid 'firmware_ota_note_battery\([\s\S]*?\);[\s\S]*?ble_hid_resync_connected_status_led\(\);[\s\S]*?if\s*\(\s*!should_notify\s*\)' `
    "battery updates must resync connected BLE LED before unchanged-level early return"
Assert-Contains $audioCMake 'PRIV_REQUIRES[\s\S]*?status_led' `
    "audio stream component must declare the status_led dependency before it updates TYPE_READY"
Assert-Contains $audioHeader 'bool\s+ble_audio_stream_is_type_link_ready\(void\);' `
    "audio stream header must expose Type-link readiness separately from stream-idle readiness"
Assert-Contains $audioHeader 'bool\s+ble_audio_stream_consume_type_control_command\(const char \*command, const char \*source\);' `
    "audio stream header must expose the Type heartbeat control command parser"
Assert-Contains $audioHeader 'uint32_t\s+ble_audio_stream_type_link_poll_wait_ms\(uint32_t fallback_ms\);' `
    "audio stream header must expose heartbeat-aware poll wait adjustment"
Assert-Contains $audioHeader 'void\s+ble_audio_stream_poll_type_link\(void\);' `
    "audio stream header must expose Type heartbeat timeout polling"
Assert-Contains $audio '#include "status_led\.h"[\s\S]*?static void ble_audio_stream_set_transport_state' `
    "audio stream must include status_led before transport state updates"
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_HEARTBEAT_TIMEOUT_MS\s+12000' `
    "Type heartbeat timeout must be bounded so closing Listener-Type demotes TYPE_READY"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_transport_state_type_ready\([^)]*\)[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING' `
    "Type-link helper must remain true during active streaming/draining"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_link_ready\(void\)[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_transport_state_type_ready\(s_transport_state\)[\s\S]*?ble_audio_stream_type_heartbeat_recent\(\)' `
    "public Type-link readiness must combine notify link, streaming-capable states, and fresh Listener-Type heartbeat"
Assert-Contains $audio 'bool\s+ble_audio_stream_consume_type_control_command\([^)]*\)[\s\S]*?TYPE:READY[\s\S]*?TYPE:HB[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(true[\s\S]*?TYPE:BYE[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false' `
    "firmware must consume Type READY/HB/BYE commands on the audio control channel"
Assert-Contains $audio 'void\s+ble_audio_stream_poll_type_link\(void\)[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false,\s*"timeout"\);[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\("type_heartbeat_timeout"\)' `
    "firmware must poll Type heartbeat timeout and demote the BLE status LED"
Assert-Contains $audio 's_transport_state\s*=\s*next_state;[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\(reason\)' `
    "audio stream ready/not-ready transitions must immediately sync TYPE_READY vs HID-only connected LED"
Assert-Contains $hid 'ble_audio_stream_type_link_poll_wait_ms\([\s\S]*?ble_hid_battery_sample_interval_ms' `
    "HID battery task must shorten one wait to the Type heartbeat deadline"
Assert-Contains $hid 'ble_audio_stream_poll_type_link\(\);' `
    "HID battery task must poll Type heartbeat timeout during connected idle"
Assert-Contains $audio 's_notify_enabled\s*=\s*notify_enabled;[\s\S]*?!notify_enabled[\s\S]*?s_type_heartbeat_active\s*=\s*false' `
    "disabling notify must clear stale Type heartbeat state"
Assert-Contains $audio 's_conn_handle\s*=\s*BLE_HS_CONN_HANDLE_NONE;[\s\S]*?s_type_heartbeat_active\s*=\s*false' `
    "BLE disconnect must clear stale Type heartbeat state"
Assert-Contains (Read-RepoFile "components\voice_recording_control\voice_recording_control.c") 'ble_audio_stream_consume_type_control_command\(command,\s*source\)[\s\S]*?return ESP_OK;[\s\S]*?power_manager_record_activity\("voice_recording_ble_control"\)' `
    "Type heartbeat control writes must be consumed before recording user activity"
Assert-Contains $statusLed 'STATUS_LED_STATUS_WINDOW_MS\s+6000U' `
    "connected status window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONFIDENCE_MS\s+8000U' `
    "connected BLE confidence window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS\s+120U' `
    "battery idle BLE heartbeat must be brief"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT\s+14U[\s\S]*?STATUS_LED_BLE_CONNECTED_STEADY_PERCENT\s+STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT' `
    "connected BLE steady brightness must stay at the current low visible level"
Assert-Contains $statusLed 'case STATUS_LED_BLE_CONNECTED:\s*\n\s*case STATUS_LED_BLE_TYPE_READY:\s*\{[\s\S]*?const bool type_ready = s_state\.ble_state == STATUS_LED_BLE_TYPE_READY;[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?STATUS_LED_BLE_CONNECTED_STEADY_PERCENT[\s\S]*?!type_ready && ble_elapsed_ms < STATUS_LED_BLE_CONNECTED_CONFIRM_MS[\s\S]*?else if \(!type_ready\)[\s\S]*?status_led_double_pulse_on\(ble_elapsed_ms,\s*2000U\)[\s\S]*?else\s*\{[\s\S]*?steady_percent' `
    "HID-only connected must blink after the brief connect confirm; TYPE_READY is the steady connected state"
if ($statusLed -match 'case STATUS_LED_BLE_CONNECTED:\s*\n\s*case STATUS_LED_BLE_TYPE_READY:\s*\{[\s\S]*?else if \(confidence \|\| status_window\)') {
    throw "verify_ble_status_led_connected_sync failed: HID-only connected must not use the confidence/status window as a steady state"
}
Assert-Contains $statusLed 'static\s+uint8_t\s+status_led_low_power_ble_percent_locked\(uint32_t ble_elapsed_ms\)[\s\S]*?case STATUS_LED_BLE_CONNECTED:[\s\S]*?STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?STATUS_LED_LOW_POWER_BLE_CONNECTED_PERCENT[\s\S]*?case STATUS_LED_BLE_TYPE_READY:[\s\S]*?return STATUS_LED_LOW_POWER_BLE_CONNECTED_PERCENT;' `
    "battery idle must blink HID-only connected but latch TYPE_READY"
Assert-Contains $statusLed 'const bool active_work = s_state\.recording_active \|\| s_state\.processing_active;' `
    "status LED renderer must define active work for recording/processing visibility"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\s+14U[\s\S]*?percent = \(status_window \|\| active_work\)[\s\S]*?\? STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT[\s\S]*?: STATUS_LED_LOW_POWER_PWR_PERCENT;' `
    "battery PWR active-rendering window must stay readable during active work and then hand off to the low-power level"
Assert-Contains $statusDoc 'BLE Connection Source Of Truth' `
    "status LED documentation must describe the BLE connection source of truth"

$modelState = "disconnected"
$connected = $false

function Apply-ModelEvent {
    param(
        [string]$Event
    )
    switch ($Event) {
        "advertising" {
            if (-not $script:connected) {
                $script:modelState = "pairing"
            }
        }
        "directed_advertising" {
            if (-not $script:connected) {
                $script:modelState = "reconnecting"
            }
        }
        "connect" {
            $script:connected = $true
            $script:modelState = "connected"
        }
        "stale_adv_complete" {
            if ($script:connected) {
                $script:modelState = "connected"
            } else {
                $script:modelState = "pairing"
            }
        }
        "disconnect" {
            $script:connected = $false
            $script:modelState = "reconnecting"
        }
        default {
            throw "unknown model event: $Event"
        }
    }
}

Apply-ModelEvent "advertising"
Apply-ModelEvent "connect"
Apply-ModelEvent "stale_adv_complete"
if ($modelState -ne "connected") {
    throw "verify_ble_status_led_connected_sync failed: pairing-to-connected model regressed to $modelState"
}

Apply-ModelEvent "disconnect"
if ($modelState -ne "reconnecting") {
    throw "verify_ble_status_led_connected_sync failed: disconnect negative transition lost reconnecting state"
}

Apply-ModelEvent "directed_advertising"
Apply-ModelEvent "connect"
Apply-ModelEvent "stale_adv_complete"
if ($modelState -ne "connected") {
    throw "verify_ble_status_led_connected_sync failed: reconnect-to-connected model regressed to $modelState"
}

Write-Host "PASS: BLE status LED connected-sync checks cover GAP/HID connected source of truth, audio-stream TYPE_READY sync, stale advertising suppression, connected battery resync after preview clears, bounded connected/type-ready brightness, active-work PWR/BLE visibility, battery idle heartbeat, and disconnect/advertising negative transitions."
