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

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Description
    )
    if ($Text -match $Pattern) {
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
Assert-NotContains $gap 'BLE identity kept stable|stable BLE identity' `
    "user-requested re-pair recovery must not advertise the old stable identity"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_rotate_static_random_identity\(const char \*context\)[\s\S]*?ble_hs_id_gen_rnd\(0,\s*&addr\)[\s\S]*?ble_hid_gap_store_static_random_identity\(addr\.val,\s*context\)[\s\S]*?ble_hid_gap_apply_static_random_identity\(addr\.val,\s*context\)' `
    "re-pair recovery must generate, persist, and apply a new static-random BLE identity"
Assert-Contains $gap 'esp_err_t\s+ble_hid_gap_forget_bonds_and_repair\(void\)[\s\S]*?rc\s*=\s*ble_store_clear\(\);[\s\S]*?s_recovery_identity_rotate_pending\s*=\s*true;[\s\S]*?identity rotates before advertising restarts[\s\S]*?ble_hid_gap_rotate_static_random_identity\("BLE recovery identity rotated for re-pair"\)[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "forget-bonds recovery must rotate identity before non-connected advertising and defer connected rotation until after disconnect"
Assert-Contains $gap 'refresh_pairing_window[\s\S]*?pairing window already active; rotating identity and refreshing advertising[\s\S]*?ble_hid_gap_rotate_static_random_identity\("BLE recovery identity rotated during active pairing window"\)[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "explicit recovery during an already-open pairing window must rotate identity again before advertising"
Assert-Contains $gap 'case BLE_GAP_EVENT_DISCONNECT:[\s\S]*?if\s*\(s_recovery_identity_rotate_pending\)[\s\S]*?ble_hid_gap_rotate_static_random_identity\("BLE recovery identity rotated after disconnect"\)[\s\S]*?s_recovery_identity_rotate_pending\s*=\s*false;[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "disconnect recovery must rotate the pending identity before advertising restarts"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?encryption failed or connection already gone[\s\S]*?ble_hid_gap_recovery_pairing_window_open\(\)[\s\S]*?s_recovery_identity_rotate_pending\s*=\s*true;[\s\S]*?stale pairing encryption failure[\s\S]*?ble_gap_terminate\(event->enc_change\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "recovery pairing window must treat encryption failures as stale host pairing and rotate identity after disconnect"
Assert-Contains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*true;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "HID connect event must mark HID-only connected state for Type-ready resync"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "HID disconnect event must still drive reconnecting LED"
Assert-Contains $hid 'static\s+status_led_ble_state_t\s+ble_hid_connected_status_led_state\([^)]*\)[\s\S]*?ble_audio_stream_is_type_led_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED' `
    "HID connected-status resync must preserve TYPE_READY while Listener-Type LED hold is ready"
Assert-Contains $hid 'static\s+void\s+ble_hid_resync_connected_status_led\([^)]*\)[\s\S]*?if\s*\(\s*!s_ble_connected\s*\)[\s\S]*?status_led_set_ble_state\(ble_hid_connected_status_led_state\(\),\s*false\);' `
    "HID connected-status resync must preserve TYPE_READY without restarting the confidence animation"
Assert-Contains $hid 'firmware_ota_note_battery\([\s\S]*?\);[\s\S]*?ble_hid_resync_connected_status_led\(\);[\s\S]*?if\s*\(\s*!should_notify\s*\)' `
    "battery updates must resync connected BLE LED before unchanged-level early return"
Assert-Contains $audioCMake 'PRIV_REQUIRES[\s\S]*?status_led' `
    "audio stream component must declare the status_led dependency before it updates TYPE_READY"
Assert-Contains $audioHeader 'bool\s+ble_audio_stream_is_type_link_ready\(void\);' `
    "audio stream header must expose Type-link readiness separately from stream-idle readiness"
Assert-Contains $audioHeader 'bool\s+ble_audio_stream_is_type_led_ready\(void\);' `
    "audio stream header must expose Type LED readiness separately from strict transport readiness"
Assert-Contains $audioHeader 'bool\s+ble_audio_stream_consume_type_control_command\(const char \*command, const char \*source\);' `
    "audio stream header must expose the Type heartbeat control command parser"
Assert-Contains $audioHeader 'void\s+ble_audio_stream_note_type_activity\(const char \*reason\);' `
    "audio stream header must expose Type activity refresh for BLE-origin host processing controls"
Assert-Contains $audioHeader 'uint32_t\s+ble_audio_stream_type_link_poll_wait_ms\(uint32_t fallback_ms\);' `
    "audio stream header must expose heartbeat-aware poll wait adjustment"
Assert-Contains $audioHeader 'void\s+ble_audio_stream_poll_type_link\(void\);' `
    "audio stream header must expose Type heartbeat timeout polling"
Assert-Contains $audio '#include "status_led\.h"[\s\S]*?static void ble_audio_stream_set_transport_state' `
    "audio stream must include status_led before transport state updates"
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_HEARTBEAT_TIMEOUT_MS\s+12000' `
    "Type heartbeat timeout must be bounded so closing Listener-Type demotes TYPE_READY"
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_LED_READY_HOLD_MS\s+30000' `
    "Type LED readiness must hold through short host heartbeat misses"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_transport_state_type_ready\([^)]*\)[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING' `
    "Type-link helper must remain true during active streaming/draining"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_link_ready\(void\)[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_transport_state_type_ready\(s_transport_state\)[\s\S]*?ble_audio_stream_type_heartbeat_recent\(\)' `
    "public Type-link readiness must combine notify link, streaming-capable states, and fresh Listener-Type heartbeat"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_type_led_link_ready\(void\)[\s\S]*?conn_handle\s*!=\s*BLE_HS_CONN_HANDLE_NONE[\s\S]*?mtu_ready' `
    "Type LED link readiness must require BLE connection/MTU but not active audio notify"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_led_ready\(void\)[\s\S]*?ble_audio_stream_type_led_link_ready\(\)[\s\S]*?ble_audio_stream_type_heartbeat_led_recent\(\)' `
    "public Type LED readiness must use the heartbeat hold without requiring the audio notify path"
Assert-Contains $audio 'ble_audio_stream_sync_status_led_for_type_link[\s\S]*?ble_audio_stream_type_led_link_ready\(\)[\s\S]*?ble_audio_stream_is_type_led_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED' `
    "Type LED sync must use LED readiness, not strict audio notify readiness"
Assert-Contains $audio 'bool\s+ble_audio_stream_consume_type_control_command\([^)]*\)[\s\S]*?TYPE:READY[\s\S]*?TYPE:HB[\s\S]*?ble_audio_stream_note_type_activity\(command\)[\s\S]*?TYPE:BYE[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false' `
    "firmware must consume Type READY/HB/BYE commands on the audio control channel"
Assert-Contains $audio 'void\s+ble_audio_stream_note_type_activity\([^)]*\)[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(\s*true[\s\S]*?ble_audio_stream_sync_status_led_for_type_link' `
    "Type activity helper must refresh heartbeat and immediately resync the BLE LED"
Assert-Contains $audio 'void\s+ble_audio_stream_poll_type_link\(void\)[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false,\s*"timeout"\);[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\("type_heartbeat_timeout"\)[\s\S]*?type_heartbeat_led_grace_timeout' `
    "firmware must poll strict Type heartbeat timeout first, then demote the BLE status LED only after the LED grace expires"
Assert-Contains $audio 's_transport_state\s*=\s*next_state;[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\(reason\)' `
    "audio stream ready/not-ready transitions must immediately sync TYPE_READY vs HID-only connected LED"
Assert-Contains $hid 'ble_audio_stream_type_link_poll_wait_ms\([\s\S]*?ble_hid_battery_sample_interval_ms' `
    "HID battery task must shorten one wait to the Type heartbeat deadline"
Assert-Contains $hid 'ble_audio_stream_poll_type_link\(\);' `
    "HID battery task must poll Type heartbeat timeout during connected idle"
Assert-Contains $audio 's_notify_enabled\s*=\s*notify_enabled;[\s\S]*?!notify_enabled[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?s_type_heartbeat_led_ready_until_tick\s*=\s*0' `
    "disabling notify must clear stale Type heartbeat and LED-hold state"
Assert-Contains $audio 's_conn_handle\s*=\s*BLE_HS_CONN_HANDLE_NONE;[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?s_type_heartbeat_led_ready_until_tick\s*=\s*0' `
    "BLE disconnect must clear stale Type heartbeat and LED-hold state"
Assert-Contains (Read-RepoFile "components\voice_recording_control\voice_recording_control.c") 'ble_audio_stream_consume_type_control_command\(command,\s*source\)[\s\S]*?return ESP_OK;[\s\S]*?power_manager_record_activity\("voice_recording_ble_control"\)' `
    "Type heartbeat control writes must be consumed before recording user activity"
Assert-Contains (Read-RepoFile "components\voice_recording_control\voice_recording_control.c") 'voice_recording_control_source_is_ble_audio_control\([^)]*\)[\s\S]*?strcmp\(source,\s*"ble_audio_control"\)\s*==\s*0[\s\S]*?voice_recording_control_note_ble_type_processing_activity[\s\S]*?ble_audio_stream_note_type_activity\(reason\)[\s\S]*?voice_recording_control_host_processing_start\([^)]*\)[\s\S]*?voice_recording_control_note_ble_type_processing_activity\(source,\s*"host_processing_start"\)' `
    "BLE-origin processing controls must refresh Type-ready LED state before processing changes"
Assert-Contains $statusLed 'STATUS_LED_STATUS_WINDOW_MS\s+6000U' `
    "connected status window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONFIDENCE_MS\s+8000U' `
    "connected BLE confidence window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS\s+120U' `
    "battery idle BLE heartbeat must be brief"
Assert-Contains $statusLed 'STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT\s+14U' `
    "TYPE_READY must use steady blue"
Assert-Contains $statusLed 'case STATUS_LED_BLE_CONNECTED:[\s\S]*?if\s*\(\s*status_led_ota_ble_steady_locked\(now_ms\)\s*\)[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?break;\s*case STATUS_LED_BLE_TYPE_READY:[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT' `
    "HID-only connected must keep BLE dark unless OTA is active, while TYPE_READY stays steady blue"
Assert-NotContains $statusLed 'status_led_connected_hid_only_percent_locked|STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS|STATUS_LED_BLE_CONNECTED_CONFIRM_MS|STATUS_LED_BLE_CONNECTED_BASE_PERCENT' `
    "HID-only connected must not keep the old blue heartbeat/confirmation renderer"
Assert-Contains $statusLed 'static\s+uint8_t\s+status_led_low_power_ble_percent_locked\(uint32_t ble_elapsed_ms\)[\s\S]*?case STATUS_LED_BLE_PAIRING:[\s\S]*?STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT[\s\S]*?case STATUS_LED_BLE_CONNECTED:\s*\n\s*case STATUS_LED_BLE_TYPE_READY:[\s\S]*?return 0U;' `
    "connected and TYPE_READY BLE must stay dark after idle"
Assert-Contains $statusLed 'const bool active_work = s_state\.recording_active \|\| s_state\.processing_active \|\| s_state\.ota_active;' `
    "status LED renderer must define active work for recording/processing/OTA visibility"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\s+14U[\s\S]*?percent = \(status_window \|\| active_work\)[\s\S]*?\? STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT[\s\S]*?: STATUS_LED_LOW_POWER_PWR_PERCENT;' `
    "battery PWR active-rendering window must stay readable during active work and then hand off to the low-power level"
Assert-Contains $statusDoc '30 second Type-ready hold' `
    "status LED documentation must describe the active Type-ready LED hold"
Assert-Contains $statusDoc 'rotates the stored BLE static-random identity before advertising again[\s\S]*?old host must pair again' `
    "status LED documentation must describe why user-requested re-pair cannot silently reconnect to the old host"

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

Write-Host "PASS: BLE status LED connected-sync checks cover GAP/HID connected source of truth, recovery identity rotation before re-pair advertising, audio-stream TYPE_READY sync with LED hold, stale advertising suppression, connected battery resync after preview clears, HID-only BLE-dark versus steady TYPE_READY brightness, active-work PWR/OTA visibility, idle connected/TYPE_READY BLE dark, and disconnect/advertising negative transitions."
