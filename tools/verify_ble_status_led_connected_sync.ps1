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
$powerManager = Read-RepoFile "components\power_manager\power_manager.c"
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

$typeRecoveryAdvIndex = $gap.IndexOf("static bool ble_hid_gap_configure_type_controlled_recovery_adv_fields(void)")
$normalAdvIndex = $gap.IndexOf("static esp_err_t ble_hid_gap_refresh_configured_device_name", $typeRecoveryAdvIndex)
if ($typeRecoveryAdvIndex -lt 0 -or $normalAdvIndex -lt 0 -or $normalAdvIndex -le $typeRecoveryAdvIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate type-controlled recovery advertising function"
}
$typeRecoveryAdv = $gap.Substring($typeRecoveryAdvIndex, $normalAdvIndex - $typeRecoveryAdvIndex)

Assert-Contains $gap 'ble_hid_gap_connection_snapshot_t\s+conn\s*=\s*ble_hid_gap_connection_snapshot\(\);' `
    "GAP advertising entry must read the shared connection snapshot"
Assert-Contains $gap 'if\s*\(\s*conn\.connected\s*\)\s*\{[\s\S]*?NimBLE advertising skipped: GAP already connected[\s\S]*?if\s*\(\s*conn\.secure_connected\s*\)\s*\{[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*false\);[\s\S]*?return ESP_OK;' `
    "GAP advertising must skip stale advertising while connected and restore connected LED only after security succeeds"
Assert-Order $advStart 'if (conn.connected)' 'if (ble_gap_adv_active())' `
    "connected guard must run before advertising-active handling"
Assert-Order $advStart 'if (conn.connected)' 'status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);' `
    "connected guard must run before directed advertising can set reconnecting LED"
Assert-Order $advStart 'if (conn.connected)' 'pairing_window' `
    "connected guard must run before undirected advertising chooses pairing or reconnecting LED"
Assert-Contains $advStart 'status_led_set_ble_state\(\s*pairing_window\s*\?\s*STATUS_LED_BLE_PAIRING\s*:\s*STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "undirected advertising must show pairing only during explicit recovery pairing windows and reconnecting otherwise"
Assert-Contains $gap 'BLE reconnect request kept existing active advertising[\s\S]*?ble_hid_gap_recovery_pairing_window_open\(\)\s*\?\s*STATUS_LED_BLE_PAIRING\s*:\s*STATUS_LED_BLE_RECONNECTING' `
    "reconnect requests must not turn normal active advertising into pairing LED state"
Assert-NotContains $gap 'case BLE_GAP_EVENT_CONNECT:[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "GAP connect event must not treat an unauthenticated Windows link as a real connected LED state"
Assert-Contains $gap 'case BLE_GAP_EVENT_CONNECT:[\s\S]*?event->connect\.status\s*!=\s*0[\s\S]*?ble_hid_gap_connection_snapshot_t\s+stale_conn\s*=\s*ble_hid_gap_connection_snapshot\(\)[\s\S]*?connection failed while previous GAP link was still marked connected[\s\S]*?ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\)[\s\S]*?ble_audio_stream_on_gap_disconnect\(stale_conn\.conn_handle\)' `
    "GAP connect-failure path must clear a stale previous connection when Windows drops without a normal disconnect event"
Assert-Contains $gap 'ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\);[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "GAP disconnect event must clear connected before reconnecting LED"
Assert-Contains $gap 'case BLE_GAP_EVENT_DISCONNECT:[\s\S]*?ble_hid_gap_get_bonded_peer_count\(&bonded_peer_count\)[\s\S]*?bonded_peer_count\s*>\s*0[\s\S]*?keeping pairing window visible until secure reconnect[\s\S]*?ble_hid_gap_start_advertising\(\)[\s\S]*?ble_hid_gap_hold_recovery_pairing_led\("ble_recovery_pairing_window_after_disconnect"\)' `
    "recovery disconnect must keep pairing LED visible through bond churn until secure reconnect"
Assert-NotContains $gap 'ble_hid_gap_close_recovery_pairing_window\("bond restored after recovery disconnect"\)' `
    "recovery disconnect must not close the pairing window merely because Windows recreated a bond"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_refresh_configured_device_name\(const char \*context\)[\s\S]*?listener_device_get_ble_name\(\)[\s\S]*?ble_svc_gap_device_name_set\(device_name\)[\s\S]*?ble_svc_gap_device_appearance_set\(s_adv_appearance\)[\s\S]*?ble_hid_gap_configure_normal_adv_fields\(\)[\s\S]*?device_settings_mark_ble_name_applied\(\)' `
    "advertising must refresh the currently configured BLE name plus GAP appearance and mark it applied"
Assert-Contains $typeRecoveryAdv 's_adv_fields\.flags\s*=\s*BLE_HS_ADV_F_DISC_GEN\s*\|\s*BLE_HS_ADV_F_BREDR_UNSUP;[\s\S]*?s_scan_rsp_fields\.uuids128\s*=\s*&s_audio_stream_service_uuid;[\s\S]*?return\s+true;' `
    "Type-controlled recovery advertising must keep the Listener name/audio service discoverable without advertising as a HID keyboard"
Assert-NotContains $typeRecoveryAdv 's_adv_fields\.appearance_is_present\s*=\s*1' `
    "Type-controlled recovery advertising must not include HID appearance that can trigger a Windows keyboard pairing toast"
Assert-NotContains $typeRecoveryAdv 's_adv_fields\.uuids16\s*=\s*&s_hid_service_uuid' `
    "Type-controlled recovery advertising must not include the HID UUID that can trigger a Windows keyboard pairing toast"
Assert-Contains $gap '#define\s+BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS\s+0LL' `
    "Listener must not advertise a Swift Pair first-pairing window by default because that raises the Windows Connect toast"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_init\(uint16_t appearance, const char \*device_name\)[\s\S]*?ble_svc_gap_device_appearance_set\(s_adv_appearance\)[\s\S]*?ble_hs_cfg\.sm_io_cap\s*=\s*BLE_SM_IO_CAP_NO_IO;[\s\S]*?ble_hs_cfg\.sm_bonding\s*=\s*1;[\s\S]*?ble_hs_cfg\.sm_mitm\s*=\s*0;[\s\S]*?ble_hs_cfg\.sm_sc\s*=\s*0;' `
    "BLE HID advertising init must publish keyboard GAP appearance and use legacy-compatible no-IO bonding for Windows pairing"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_start\(void\)[\s\S]*?ble_hid_gap_refresh_configured_device_name\("advertising_start"\)' `
    "advertising start must use the latest configured BLE name"
Assert-Contains $gap 'esp_err_t\s+ble_hid_gap_forget_bonds_and_repair\(void\)[\s\S]*?bond_delete=async_after_disconnect[\s\S]*?ble_hid_gap_open_recovery_pairing_window\([^)]*\);[\s\S]*?ble_hid_gap_schedule_recovery_bond_delete[\s\S]*?stable identity will advertise after async local bond delete following disconnect[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "forget-bonds recovery must avoid synchronous full-store erase, open the pairing window, delete the local bond asynchronously, and advertise the current stable identity"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_start\(void\)[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?NimBLE advertising deferred: recovery async local bond delete pending[\s\S]*?STATUS_LED_BLE_PAIRING' `
    "advertising must wait while recovery async local bond delete is pending"
Assert-Contains $gap 'case BLE_GAP_EVENT_CONNECT:[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?recovery: rejecting connection while async local bond delete is pending[\s\S]*?ble_gap_terminate\(event->connect\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "recovery must reject stale Windows connections while async local bond delete is pending"
Assert-Contains $gap 'refresh_pairing_window[\s\S]*?pairing window already active; refreshing advertising with stable BLE identity[\s\S]*?ble_hid_gap_open_recovery_pairing_window\([^)]*\)[\s\S]*?ble_hid_gap_start_advertising\(\)[\s\S]*?pairing window refreshed with stable BLE identity' `
    "explicit recovery during an already-open pairing window must renew the 120s pairing window and refresh advertising with the current stable identity"
Assert-Contains $gap 'case BLE_GAP_EVENT_CONNECT:[\s\S]*?const bool recovery_pairing_window\s*=\s*ble_hid_gap_recovery_pairing_window_open\(\);[\s\S]*?conn_desc_valid\s*&&\s*recovery_pairing_window[\s\S]*?waiting for central-led security[\s\S]*?else\s+if\s*\(conn_desc_valid\)[\s\S]*?ble_gap_security_initiate\(event->connect\.conn_handle\)' `
    "recovery pairing window must let Windows central-led pairing lead security before the normal non-recovery security request path"
Assert-NotContains $gap 'conn_desc_valid\s*&&\s*recovery_pairing_window[\s\S]*?ble_hid_gap_request_recovery_security_once\(event->connect\.conn_handle,\s*"connect"\)' `
    "recovery connect must not immediately initiate security before Windows starts pairing"
Assert-Contains $gap 'case BLE_GAP_EVENT_SUBSCRIBE:[\s\S]*?ble_hid_gap_request_recovery_security_once\(event->subscribe\.conn_handle,\s*"subscribe"\);[\s\S]*?case BLE_GAP_EVENT_MTU:[\s\S]*?ble_hid_gap_request_recovery_security_once\(event->mtu\.conn_handle,\s*"mtu"\);' `
    "recovery pairing must keep MTU/subscribe security requests as idempotent backstops"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?desc\.sec_state\.encrypted\s*\|\|\s*desc\.sec_state\.bonded[\s\S]*?ble_hid_gap_note_secure_connection\([\s\S]*?"secure connection established"\);' `
    "successful recovery pairing must switch from pairing LED to connected find-Type double flash"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?encryption failed or connection already gone[\s\S]*?ble_hid_gap_recovery_pairing_window_open\(\)[\s\S]*?pairing encryption failure[\s\S]*?keeping pairing advertising available for Windows retry' `
    "recovery pairing window must keep pairing discoverability available while Windows retries after encryption failure"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?ble_gap_terminate\(event->enc_change\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "ENC_CHANGE may terminate only stale encrypted connections that arrive while async local bond delete is pending"
Assert-NotContains $gap 'encryption failed or connection already gone[\s\S]*?ble_gap_terminate\(event->enc_change\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "recovery encryption failure handling must not terminate the Windows pairing connection from the failure branch"
Assert-NotContains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "HID connect event must not mark an unauthenticated Windows link as connected LED"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "HID disconnect event must still drive reconnecting LED"
Assert-Contains $hid 'static\s+status_led_ble_state_t\s+ble_hid_connected_status_led_state\([^)]*\)[\s\S]*?ble_audio_stream_is_type_led_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED' `
    "HID connected-status resync must preserve TYPE_READY while Listener-Type LED hold is ready"
Assert-Contains $hid 'static\s+void\s+ble_hid_resync_connected_status_led\([^)]*\)[\s\S]*?!ble_hid_gap_is_securely_connected\(\)[\s\S]*?status_led_set_ble_state\(ble_hid_connected_status_led_state\(\),\s*false\);' `
    "HID connected-status resync must wait for GAP security and preserve TYPE_READY without restarting the confidence animation"
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
Assert-Contains $audio 'extern\s+void\s+power_manager_set_ble_connected\(bool connected\).*__attribute__\(\(weak\)\)' `
    "audio stream must weak-link power manager BLE connection sync"
Assert-Contains $audio 'extern\s+bool\s+ble_hid_gap_is_securely_connected\(void\).*__attribute__\(\(weak\)\)' `
    "audio stream must weak-link HID secure state before clearing Type-link power state"
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_HEARTBEAT_TIMEOUT_MS\s+45000' `
    "Type heartbeat timeout must bridge WinRT/Windows reconnect jitter while BYE still clears immediately"
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_LED_READY_HOLD_MS\s+45000' `
    "Type LED readiness must match the heartbeat window through short host heartbeat misses"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_transport_state_type_ready\([^)]*\)[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING' `
    "Type-link helper must remain true during active streaming/draining"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_link_ready\(void\)[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_transport_state_type_ready\(s_transport_state\)[\s\S]*?ble_audio_stream_type_heartbeat_recent\(\)' `
    "public Type-link readiness must combine notify link, streaming-capable states, and fresh Listener-Type heartbeat"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_type_led_link_ready\(void\)[\s\S]*?conn_handle\s*!=\s*BLE_HS_CONN_HANDLE_NONE' `
    "Type LED link readiness must require a real BLE connection while accepting heartbeat as the Type-ready proof"
Assert-NotContains $audio 'static\s+bool\s+ble_audio_stream_type_led_link_ready\(void\)\s*\{[^}]*mtu_ready' `
    "Type LED link readiness must not wait for MTU after a valid Type heartbeat/control write"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_led_ready\(void\)[\s\S]*?ble_audio_stream_type_led_link_ready\(\)[\s\S]*?ble_audio_stream_type_heartbeat_led_recent\(\)' `
    "public Type LED readiness must use the heartbeat hold without requiring the audio notify path"
Assert-Contains $audio 'static\s+void\s+ble_audio_stream_sync_power_manager_for_type_link\(bool active,[\s\S]*?if\s*\(active\)[\s\S]*?power_manager_set_ble_connected\(true\);[\s\S]*?!ble_audio_stream_hid_secure_connected\(\)[\s\S]*?power_manager_set_ble_connected\(false\);' `
    "Type heartbeat must set power-manager BLE connected true and only clear it when HID secure connection is absent"
Assert-Contains $audio 'void\s+ble_audio_stream_on_gap_connect\(uint16_t conn_handle\)[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(false,\s*"gap_connect"\)' `
    "audio GAP connect must clear stale Type-link power state with the correct connect reason"
Assert-Contains $powerManager 'extern\s+bool\s+ble_audio_stream_is_type_link_ready\(void\).*__attribute__\(\(weak\)\)' `
    "power manager must weak-link Type-link readiness"
Assert-Contains $powerManager 'power_manager_refresh_ble_connection_locked\([^)]*\)[\s\S]*?ble_hid_gap_is_connected\(\)[\s\S]*?\|\|[\s\S]*?ble_audio_stream_is_type_link_ready\(\)' `
    "power manager periodic BLE refresh must treat HID/GAP and Type-link readiness as one effective connection"
Assert-Contains $powerManager 'void\s+power_manager_set_ble_connected\(bool connected\)[\s\S]*?bool\s+effective_connected[\s\S]*?connected\s*\|\|[\s\S]*?ble_audio_stream_is_type_link_ready\(\)[\s\S]*?power_manager_apply_ble_connection_change_locked\(effective_connected,\s*now_ms\)' `
    "power manager BLE callbacks must not let HID false overwrite a fresh Type heartbeat link"
Assert-Contains $audio 'static\s+void\s+ble_audio_stream_note_control_write_connection\(uint16_t conn_handle\)[\s\S]*?s_conn_handle\s*=\s*conn_handle' `
    "audio control writes must adopt their live GAP connection before TYPE heartbeat LED sync"
Assert-Contains $audio 'ble_audio_stream_sync_status_led_for_type_link[\s\S]*?ble_audio_stream_type_led_link_ready\(\)[\s\S]*?ble_audio_stream_is_type_led_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED' `
    "Type LED sync must use LED readiness, not strict audio notify readiness"
Assert-Contains $audio 'bool\s+ble_audio_stream_consume_type_control_command\([^)]*\)[\s\S]*?TYPE:READY[\s\S]*?TYPE:HB[\s\S]*?ble_audio_stream_note_type_activity\(command\)[\s\S]*?TYPE:BYE[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false' `
    "firmware must consume Type READY/HB/BYE commands on the audio control channel"
Assert-Contains $audio 'static\s+int\s+ble_audio_stream_handle_control_write\(uint16_t conn_handle,[\s\S]*?ble_audio_stream_note_control_write_connection\(conn_handle\);[\s\S]*?ble_audio_stream_consume_type_control_command' `
    "audio control write handler must record conn_handle before consuming TYPE heartbeat"
Assert-Contains $audio 'void\s+ble_audio_stream_note_type_activity\([^)]*\)[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(\s*true[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(\s*true[\s\S]*?ble_audio_stream_sync_status_led_for_type_link' `
    "Type activity helper must refresh heartbeat and immediately resync power manager plus BLE LED"
Assert-Contains $audio 'void\s+ble_audio_stream_poll_type_link\(void\)[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(false,\s*"timeout"\);[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(false,\s*"type_heartbeat_timeout"\);[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\("type_heartbeat_timeout"\)[\s\S]*?type_heartbeat_led_grace_timeout' `
    "firmware must poll strict Type heartbeat timeout first, then demote the BLE status LED only after the LED grace expires"
Assert-Contains $audio 's_transport_state\s*=\s*next_state;[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_sync_status_led_for_type_link\(reason\)' `
    "audio stream ready/not-ready transitions must immediately sync TYPE_READY vs HID-only connected LED"
Assert-Contains $hid 'ble_audio_stream_type_link_poll_wait_ms\([\s\S]*?ble_hid_battery_sample_interval_ms' `
    "HID battery task must shorten one wait to the Type heartbeat deadline"
Assert-Contains $hid 'ble_audio_stream_poll_type_link\(\);' `
    "HID battery task must poll Type heartbeat timeout during connected idle"
Assert-Contains $audio 's_notify_enabled\s*=\s*notify_enabled;[\s\S]*?!notify_enabled[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?s_type_heartbeat_led_ready_until_tick\s*=\s*0' `
    "disabling notify must clear stale Type heartbeat and LED-hold state"
Assert-Contains $audio 's_notify_enabled\s*=\s*notify_enabled;[\s\S]*?!notify_enabled[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?portEXIT_CRITICAL[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(false,\s*"notify_disabled"\)' `
    "disabling notify must clear Type-link power-manager connected state when HID is not secure"
Assert-Contains $audio 's_conn_handle\s*=\s*BLE_HS_CONN_HANDLE_NONE;[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?s_type_heartbeat_led_ready_until_tick\s*=\s*0' `
    "BLE disconnect must clear stale Type heartbeat and LED-hold state"
Assert-Contains $audio 's_conn_handle\s*=\s*BLE_HS_CONN_HANDLE_NONE;[\s\S]*?s_type_heartbeat_active\s*=\s*false[\s\S]*?portEXIT_CRITICAL[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(false,\s*"gap_disconnect"\)' `
    "GAP disconnect must clear Type-link power-manager connected state when HID is not secure"
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
Assert-Contains $statusLed 'STATUS_LED_BLE_RECONNECT_MIN_PERCENT\s+0U' `
    "ordinary reconnecting must have an off floor so unconnected state cannot look connected"
Assert-Contains $statusLed 'STATUS_LED_BLE_RECONNECT_MAX_PERCENT\s+0U' `
    "ordinary reconnecting must stay dark in active rendering"
Assert-Contains $statusLed 'case STATUS_LED_BLE_RECONNECTING:[\s\S]*?status_led_token_locked\(\s*ble_blue,\s*STATUS_LED_BLE_RECONNECT_MIN_PERCENT,\s*false\s*\)[\s\S]*?break;' `
    "active reconnecting must render dark, not a connected/find-Type cue"
Assert-NotContains $statusLed 'case STATUS_LED_BLE_RECONNECTING:\s*\{(?:(?!break;)[\s\S])*?status_led_double_pulse_on' `
    "reconnecting must not share the connected/find-Type double flash"
Assert-NotContains $statusLed 'case STATUS_LED_BLE_RECONNECTING:\s*\{(?:(?!break;)[\s\S])*?status_led_blink_on' `
    "ordinary reconnecting must not blink like a connected cue"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s+2000U' `
    "HID-only connected must use a bounded find-Type double-flash period"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_FLOOR_PERCENT\s+10U' `
    "HID-only connected find-Type must keep the restored low blue floor"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_PULSE_PERCENT\s+STATUS_LED_BLE_ATTENTION_PERCENT' `
    "HID-only connected find-Type flash must stay visible above the restored low blue floor while reconnecting stays dark"
Assert-Contains $statusLed 'case STATUS_LED_BLE_CONNECTED:[\s\S]*?if\s*\(\s*status_led_ota_ble_steady_locked\(now_ms\)\s*\)[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?STATUS_LED_BLE_CONNECTED_FIND_TYPE_PULSE_PERCENT[\s\S]*?STATUS_LED_BLE_CONNECTED_FIND_TYPE_FLOOR_PERCENT[\s\S]*?status_led_token_locked\(\s*ble_blue,\s*percent,\s*false\s*\)[\s\S]*?break;\s*case STATUS_LED_BLE_TYPE_READY:[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT' `
    "HID-only connected must show the restored low-floor blue find-Type double flash, while TYPE_READY stays steady blue"
Assert-NotContains $statusLed 'status_led_connected_hid_only_percent_locked|STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS|STATUS_LED_BLE_CONNECTED_CONFIRM_MS|STATUS_LED_BLE_CONNECTED_BASE_PERCENT' `
    "HID-only connected must not keep the old low-base heartbeat/confirmation renderer"
Assert-Contains $statusLed 'static\s+uint8_t\s+status_led_low_power_ble_percent_locked\(uint32_t ble_elapsed_ms\)[\s\S]*?case STATUS_LED_BLE_PAIRING:[\s\S]*?STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?STATUS_LED_LOW_POWER_BLE_ATTENTION_PERCENT[\s\S]*?case STATUS_LED_BLE_RECONNECTING:\s*\n\s*case STATUS_LED_BLE_CONNECTED:\s*\n\s*case STATUS_LED_BLE_TYPE_READY:[\s\S]*?return 0U;' `
    "reconnecting, connected, and TYPE_READY BLE must stay dark after idle"
Assert-Contains $statusLed 'const bool active_work = s_state\.recording_active \|\| s_state\.processing_active \|\| s_state\.ota_active;' `
    "status LED renderer must define active work for recording/processing/OTA visibility"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\s+14U[\s\S]*?percent = \(status_window \|\| active_work\)[\s\S]*?\? STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT[\s\S]*?: STATUS_LED_LOW_POWER_PWR_PERCENT;' `
    "battery PWR active-rendering window must stay readable during active work and then hand off to the low-power level"
Assert-Contains $statusDoc '30 second Type-ready hold' `
    "status LED documentation must describe the active Type-ready LED hold"
Assert-Contains $statusDoc 'keeps the stable BLE identity[\s\S]*?deletes the firmware-side old peer asynchronously after disconnect before advertising can restart[\s\S]*?create a new bond through the recovery window' `
    "status LED documentation must describe stable-identity recovery and async local bond-delete semantics"

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

Write-Host "PASS: BLE status LED connected-sync checks cover GAP/HID connected source of truth, stable-identity re-pair advertising, audio-stream TYPE_READY sync with LED hold, stale advertising suppression, connected battery resync after preview clears, HID-only find-Type double flash versus steady TYPE_READY brightness, active-work PWR/OTA visibility, idle connected/TYPE_READY BLE dark, and disconnect/advertising negative transitions."
