param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ReadbackLog = ""
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
$sdkDefaults = Read-RepoFile "sdkconfig.defaults"
$sdkDefaultsEsp32s3 = Read-RepoFile "sdkconfig.defaults.esp32s3"
$pairingContract = Read-RepoFile "third_party\denzic-platform\ble_pairing\embedded\c\include\denzic_ble_pairing_v1_generated.h"

$advStartIndex = $gap.IndexOf("esp_err_t esp_hid_ble_gap_adv_start(void)")
$advEndIndex = $gap.IndexOf("/*`n * CONTROLLER INIT", $advStartIndex)
if ($advEndIndex -lt 0) {
    $advEndIndex = $gap.IndexOf("/*`r`n * CONTROLLER INIT", $advStartIndex)
}
if ($advStartIndex -lt 0 -or $advEndIndex -lt 0 -or $advEndIndex -le $advStartIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate esp_hid_ble_gap_adv_start"
}
$advStart = $gap.Substring($advStartIndex, $advEndIndex - $advStartIndex)

$recoveryHelperStartIndex = $gap.IndexOf("static void ble_hid_gap_request_recovery_security_once")
$recoveryHelperEndIndex = $gap.IndexOf("static uint16_t ble_hid_gap_get_service_changed_val_handle", $recoveryHelperStartIndex)
if ($recoveryHelperStartIndex -lt 0 -or $recoveryHelperEndIndex -lt 0 -or $recoveryHelperEndIndex -le $recoveryHelperStartIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate ble_hid_gap_request_recovery_security_once"
}

function Assert-ReadbackContains {
    param(
        [string]$Text,
        [string]$Token,
        [string]$Description
    )
    if (-not $Text.Contains($Token)) {
        throw "verify_ble_status_led_connected_sync failed: $Description"
    }
}
$recoverySecurityHelper = $gap.Substring($recoveryHelperStartIndex, $recoveryHelperEndIndex - $recoveryHelperStartIndex)

$typeRecoveryAdvIndex = $gap.IndexOf("static bool ble_hid_gap_configure_type_controlled_recovery_adv_fields(void)")
$normalAdvIndex = $gap.IndexOf("static esp_err_t ble_hid_gap_refresh_configured_device_name", $typeRecoveryAdvIndex)
if ($typeRecoveryAdvIndex -lt 0 -or $normalAdvIndex -lt 0 -or $normalAdvIndex -le $typeRecoveryAdvIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate type-controlled recovery advertising function"
}
$typeRecoveryAdv = $gap.Substring($typeRecoveryAdvIndex, $normalAdvIndex - $typeRecoveryAdvIndex)

$noteTypeAudioIndex = $gap.IndexOf("bool ble_hid_gap_note_type_audio_ready(const char *reason)")
$holdPairingIndex = $gap.IndexOf("static void ble_hid_gap_hold_recovery_pairing_led", $noteTypeAudioIndex)
if ($noteTypeAudioIndex -lt 0 -or $holdPairingIndex -lt 0 -or $holdPairingIndex -le $noteTypeAudioIndex) {
    throw "verify_ble_status_led_connected_sync failed: could not isolate ble_hid_gap_note_type_audio_ready"
}
$noteTypeAudio = $gap.Substring($noteTypeAudioIndex, $holdPairingIndex - $noteTypeAudioIndex)

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
Assert-Contains $gap 'static\s+ble_hid_gap_connection_snapshot_t\s+ble_hid_gap_reconcile_connection_snapshot[\s\S]*?ble_gap_conn_find\(snapshot\.conn_handle,\s*&desc\)[\s\S]*?clearing stale BLE GAP connection state[\s\S]*?ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\)[\s\S]*?power_manager_set_ble_connected\(false\)' `
    "GAP must clear stale ghost-connected state when NimBLE no longer has the tracked connection"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_request_connection_params[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\([^)]*connection_params[^)]*\)[\s\S]*?if\s*\(\s*!conn\.connected' `
    "connection parameter updates must reconcile stale ghost-connected state before using the tracked handle"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_request_preferred_2m_phy[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\([^)]*preferred_2m_phy[^)]*\)[\s\S]*?if\s*\(\s*!conn\.connected' `
    "2M PHY requests must reconcile stale ghost-connected state before using the tracked handle"
Assert-Contains $gap 'bool\s+ble_hid_gap_is_connected\(void\)[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\("is_connected"\)\.connected' `
    "public connected queries must not return stale GAP state without NimBLE conn_find validation"
Assert-Contains $gap 'bool\s+ble_hid_gap_is_securely_connected\(void\)[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\("is_securely_connected"\)\.secure_connected' `
    "public secure-connected queries must not return stale GAP state without NimBLE conn_find validation"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_handle_disconnect[\s\S]*?duplicate_disconnect[\s\S]*?ble_hid_gap_connection_snapshot_t\s+active_snapshot\s*=\s*ble_hid_gap_connection_snapshot\(\)[\s\S]*?!active_snapshot\.connected\s*\|\|\s*active_snapshot\.conn_handle\s*!=\s*conn_handle[\s\S]*?duplicate disconnect event ignored[\s\S]*?duplicate disconnect event matches active tracked connection; processing to clear state[\s\S]*?ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\)' `
    "duplicate disconnect filtering must not swallow a reused conn_handle disconnect after Windows reconnects quickly"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_start\(void\)[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\("advertising_start"\)[\s\S]*?if\s*\(\s*conn\.connected\s*\)' `
    "advertising start must reconcile ghost-connected GAP state before skipping advertising"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_forget_bonds_and_repair_inner[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\("recovery_pairing_reset"\)[\s\S]*?terminate reported no active connection; clearing stale GAP state and continuing to pairable advertising[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "recovery pairing reset must recover from stale connected state and continue to pairable advertising"
Assert-Contains $gap 'esp_err_t\s+ble_hid_gap_apply_pending_ble_name\(void\)[\s\S]*?ble_hid_gap_reconcile_connection_snapshot\("ble_name_apply"\)[\s\S]*?BLE name apply terminate failed rc=%d; attempting advertising restart path[\s\S]*?rc != BLE_HS_ENOTCONN && rc != BLE_HS_EINVAL[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "BLE name apply must recover from stale ghost-connected state instead of failing before advertising the new name"
Assert-Contains $gap 'ble_hid_gap_apply_pending_ble_name\(void\)[\s\S]*?!device_settings_ble_name_pending_restart\(\)[\s\S]*?BLE name apply skipped: no pending name change; preserving current connection/recovery state[\s\S]*?return ESP_OK;[\s\S]*?s_explicit_recovery_required_after_security_failure\s*=\s*false' `
    "a no-op BLE name apply must return before clearing the explicit-recovery hold"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_handle_disconnect[\s\S]*?ble_hid_gap_get_bonded_peer_count\(&recovery_bonded_peer_count\)[\s\S]*?recovery_bonded_peer_count\s*>\s*0[\s\S]*?keeping pairing window visible until secure reconnect[\s\S]*?ble_hid_gap_start_advertising\(\)[\s\S]*?ble_hid_gap_hold_recovery_pairing_led\("ble_recovery_pairing_window_after_disconnect"\)' `
    "recovery disconnect must keep pairing LED visible through bond churn until secure reconnect"
Assert-NotContains $gap 'ble_hid_gap_close_recovery_pairing_window\("bond restored after recovery disconnect"\)' `
    "recovery disconnect must not close the pairing window merely because Windows recreated a bond"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_close_recovery_pairing_window\(const char \*reason\)[\s\S]*?strcmp\(reason,\s*"pairing_window_expired"\)\s*==\s*0[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\)' `
    "expired recovery pairing windows must not leave the BLE LED state stuck on pairing after the pairable window has ended"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_refresh_configured_device_name\(const char \*context\)[\s\S]*?listener_device_get_ble_name\(\)[\s\S]*?ble_svc_gap_device_name_set\(device_name\)[\s\S]*?ble_svc_gap_device_appearance_set\(s_adv_appearance\)[\s\S]*?ble_hid_gap_configure_normal_adv_fields\(\)[\s\S]*?device_settings_mark_ble_name_applied\(\)' `
    "advertising must refresh the currently configured BLE name plus GAP appearance and mark it applied"
Assert-Contains $typeRecoveryAdv 's_adv_fields\.flags\s*=\s*BLE_HS_ADV_F_DISC_GEN\s*\|\s*BLE_HS_ADV_F_BREDR_UNSUP;[\s\S]*?s_adv_fields\.appearance_is_present\s*=\s*1;[\s\S]*?s_adv_fields\.uuids16\s*=\s*&s_hid_service_uuid;[\s\S]*?s_scan_rsp_fields\.uuids128\s*=\s*&s_audio_stream_service_uuid;[\s\S]*?return\s+true;' `
    "Type-controlled recovery advertising must stay HID-pairable for Windows while keeping Type audio discovery in scan response"
Assert-NotContains $typeRecoveryAdv 's_adv_fields\.mfg_data\s*=' `
    "Type-controlled fallback advertising must keep the Swift Pair payload out of the quiet Type-only profile"
Assert-Contains $pairingContract '#define\s+DENZIC_BLE_PAIRING_V1_RECOVERY_SWIFT_PAIR_PROMPT_WINDOW_MS\s+\(45000u\)' `
    "shared pairing contract must keep the bounded 45s Swift Pair recovery window"
Assert-Contains $gap '#define\s+BLE_HID_GAP_RECOVERY_SWIFT_PAIR_PROMPT_MS\s+\(\(int64_t\)DENZIC_BLE_PAIRING_V1_RECOVERY_SWIFT_PAIR_PROMPT_WINDOW_MS\)' `
    "non-Type recovery must expose one bounded Swift Pair window so Windows native keyboard pairing can recover without Listener-Type"
Assert-Contains $pairingContract '#define\s+DENZIC_BLE_PAIRING_V1_FIRST_PAIRING_WINDOW_MS\s+\(0u\)' `
    "shared pairing contract must keep first-pair Swift Pair prompting disabled"
Assert-Contains $gap '#define\s+BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS\s+\(\(int64_t\)DENZIC_BLE_PAIRING_V1_FIRST_PAIRING_WINDOW_MS\)' `
    "Listener first pairing must avoid unsolicited Swift Pair popups while remaining visible for manual Windows pairing"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_init\(uint16_t appearance, const char \*device_name\)[\s\S]*?ble_svc_gap_device_appearance_set\(s_adv_appearance\)[\s\S]*?ble_hs_cfg\.sm_io_cap\s*=\s*BLE_SM_IO_CAP_NO_IO;[\s\S]*?ble_hs_cfg\.sm_bonding\s*=\s*1;[\s\S]*?ble_hs_cfg\.sm_mitm\s*=\s*0;[\s\S]*?legacy-compatible[\s\S]*?ble_hs_cfg\.sm_sc\s*=\s*0;' `
    "BLE HID advertising init must publish keyboard GAP appearance and use no-IO legacy-compatible SMP for Windows HID pairing"
Assert-Contains $sdkDefaults '# CONFIG_BT_CTRL_MODEM_SLEEP is not set' `
    "sdkconfig.defaults must keep BLE controller modem sleep disabled for Windows HID pairing stability"
Assert-Contains $sdkDefaultsEsp32s3 '# CONFIG_BT_CTRL_MODEM_SLEEP is not set' `
    "sdkconfig.defaults.esp32s3 must keep BLE controller modem sleep disabled for Windows HID pairing stability"
Assert-NotContains $sdkDefaults 'CONFIG_BT_CTRL_MODEM_SLEEP=y' `
    "sdkconfig.defaults must not re-enable BLE controller modem sleep"
Assert-NotContains $sdkDefaultsEsp32s3 'CONFIG_BT_CTRL_MODEM_SLEEP=y' `
    "sdkconfig.defaults.esp32s3 must not re-enable BLE controller modem sleep"
Assert-Contains $sdkDefaults 'CONFIG_BT_NIMBLE_SMP_ID_RESET=y' `
    "sdkconfig.defaults must rotate the local IRK after recovery removes every bond"
Assert-Contains $sdkDefaultsEsp32s3 'CONFIG_BT_NIMBLE_SMP_ID_RESET=y' `
    "sdkconfig.defaults.esp32s3 must rotate the local IRK after recovery removes every bond"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_register_global_event_listener_once[\s\S]*?ble_gap_event_listener_register\([\s\S]*?ble_hid_gap_global_event_listener[\s\S]*?global GAP event listener registered' `
    "BLE GAP must register a global listener before advertising so CONNECT cannot be missed by the advertising callback"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_start\(void\)[\s\S]*?ble_hid_gap_refresh_configured_device_name\("advertising_start"\)' `
    "advertising start must use the latest configured BLE name"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_forget_bonds_and_repair_inner\(\s*bool type_controlled_request,\s*bool suppress_swift_pair_prompt,\s*bool force_fresh_native_identity\)' `
    "forget-bonds recovery must keep explicit Type, prompt, and fresh-native-identity inputs"
Assert-Contains $gap 'denzic_ble_pairing_v1_refresh_existing_pairing_window\([\s\S]*?force_fresh_native_identity\)[\s\S]*?bond_delete=async_after_disconnect[\s\S]*?ble_hid_gap_open_recovery_pairing_window\(' `
    "forget-bonds recovery must bypass stale windows when fresh native identity is requested and open the reset window"
Assert-Contains $gap 'denzic_ble_pairing_v1_identity_for_recovery\([\s\S]*?DENZIC_BLE_PAIRING_V1_IDENTITY_KEEP_STABLE[\s\S]*?DENZIC_BLE_PAIRING_V1_IDENTITY_DEFER_ROTATE_UNTIL_DISCONNECT[\s\S]*?ble_hid_gap_defer_native_recovery_identity_rotation\("recovery_pairing_reset"\)' `
    "forget-bonds recovery must keep Type identity stable and defer native Windows identity rotation"
Assert-Contains $gap 'type_controlled_recovery\s*&&\s*!force_fresh_native_identity[\s\S]*?denzic_ble_pairing_v1_identity_for_recovery\(\s*keep_stable_type_identity' `
    "Type rename recovery must override stable Type identity while ordinary Type recovery remains stable"
Assert-Contains $gap 'ble_hid_gap_forget_bonds_and_repair_type_controlled_silent_fresh_identity\(void\)\s*\{\s*return\s+ble_hid_gap_forget_bonds_and_repair_inner\(true,\s*true,\s*true\);' `
    "Type rename recovery must suppress Swift Pair and force a fresh identity"
Assert-Contains $gap 'ble_hid_gap_schedule_recovery_bond_delete\([\s\S]*?stable Type-controlled[\s\S]*?rotated native Windows[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "forget-bonds recovery must avoid synchronous full-store erase, open the pairing window, delete the local bond asynchronously, keep Type identity stable, and rotate native Windows identity"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_recovery_bond_delete_task\([\s\S]*?ble_gap_adv_stop\(\)[\s\S]*?for\s*\(int index = 0; index < bonded_peer_count; \+\+index\)[\s\S]*?ble_gap_unpair\(&bonded_peers\[index\]\)' `
    "recovery bond deletion must use NimBLE unpair after advertising stops so the final deleted bond rotates the local IRK"
Assert-Contains $gap 'static\s+esp_err_t\s+ble_hid_gap_reset_local_irk_without_bonds\(void\)[\s\S]*?ble_gap_adv_active\(\)[\s\S]*?ble_gap_adv_stop\(\)[\s\S]*?ble_store_delete_local_irk\(&key\)[\s\S]*?ble_hs_pvcy_set_default_irk\(\)[\s\S]*?ble_hs_pvcy_remove_entry\(BLE_ADDR_PUBLIC, zero_addr\)[\s\S]*?ble_hs_pvcy_set_our_irk\(NULL\)[\s\S]*?ble_gap_read_local_irk\(refreshed_irk\)' `
    "native recovery with no local bonds must refresh the stored and active local IRK before pairing"
Assert-Contains $gap 'if\s*\(\s*force_fresh_native_identity\s*\|\|\s*bonded_peer_count\s*==\s*0\s*\)\s*\{\s*s_recovery_need_local_irk_reset\s*=\s*true;[\s\S]*?ble_hid_gap_defer_native_recovery_identity_rotation\("recovery_pairing_reset"\)[\s\S]*?need_recovery_flash_worker[\s\S]*?ble_hid_gap_schedule_recovery_bond_delete' `
    "fresh Type rename and disconnected zero-bond native recovery must defer complete IRK/address rotation to the internal-DRAM worker"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_recovery_bond_delete_task\([^)]*\)[\s\S]*?need_local_irk_reset\s*=\s*s_recovery_need_local_irk_reset[\s\S]*?s_recovery_need_local_irk_reset\s*=\s*false[\s\S]*?ble_hid_gap_recovery_bond_delete_set_state\(false,\s*false,\s*NULL\)[\s\S]*?if\s*\(need_local_irk_reset\)\s*\{[\s\S]*?ble_hid_gap_reset_local_irk_without_bonds\(\)[\s\S]*?ble_hid_gap_rotate_native_recovery_identity\("async_bond_delete_complete"\)[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "fresh recovery must snapshot its IRK-reset intent before worker state clear, then reset IRK and rotate address before advertising"
Assert-Contains $gap '23,\s*0,\s*1,\s*s_ble_gap_conn_handle' `
    "successful fresh-identity IRK reset must be preserved in firmware diagnostics"
Assert-Contains $gap 'disconnect_recovery_pairing_window[\s\S]*?denzic_ble_pairing_v1_orch_irk_reset_after_disconnect\([\s\S]*?ble_hid_gap_reset_local_irk_without_bonds\(\)[\s\S]*?denzic_ble_pairing_v1_rotate_before_advertising\([^)]*\)[\s\S]*?ble_hid_gap_rotate_native_recovery_identity\("recovery_disconnect"\)' `
    "connected native recovery must reset a no-bond IRK after disconnect before advertising a new identity"
Assert-Contains $gap 'if\s*\(host_deliberate_disconnect\)\s*\{[\s\S]*?s_directed_adv_pending\s*=\s*false;[\s\S]*?restarting undirected connectable advertising while retaining the bond[\s\S]*?\}\s*else\s*\{[\s\S]*?DENZIC_BLE_PAIRING_V1_ADVERTISING_AFTER_DISCONNECT_DIRECTED_RECONNECT' `
    "remote-user-terminated disconnect must retain the bond and fall through to undirected connectable advertising"
Assert-NotContains $gap 'if\s*\(host_deliberate_disconnect\)\s*\{[\s\S]{0,900}?ble_hid_gap_set_explicit_recovery_required_after_security_failure\(true\)' `
    "remote-user-terminated disconnect is not proof of unpair and must not enter the security-failure dark phase"
Assert-Contains $noteTypeAudio 'bonded_peer_count\s*==\s*0[\s\S]*?!pairing_window_open[\s\S]*?type audio ready rejected on unbonded link outside explicit recovery[\s\S]*?ble_hid_gap_set_explicit_recovery_required_after_security_failure\(true\)[\s\S]*?ble_hid_gap_explicit_recovery_led_state\(\)[\s\S]*?ble_gap_terminate\([\s\S]*?return false;' `
    "out-of-window unbonded Type heartbeat must terminate into the explicit-recovery hold"
Assert-Contains $noteTypeAudio 'type audio ready rejected before BLE bond inside explicit recovery; keeping pairing window available[\s\S]*?return false;' `
    "an already-authorized recovery window must remain available for secure pairing"
Assert-NotContains $noteTypeAudio 'bonded_peer_count\s*==\s*0[\s\S]*?ble_hid_gap_open_recovery_pairing_window' `
    "an insecure Type heartbeat must never open its own pairing window"
Assert-Contains $noteTypeAudio 'ble_gap_conn_find\(s_ble_gap_conn_handle,\s*&desc\)[\s\S]*?desc\.sec_state\.encrypted\s*\|\|\s*desc\.sec_state\.bonded[\s\S]*?type audio ready accepted on existing secure BLE connection[\s\S]*?ble_hid_gap_note_secure_connection\([\s\S]*?"type audio ready existing secure connection"[\s\S]*?return true;' `
    "Type heartbeat on an existing encrypted/bonded Windows HID connection must promote GAP secure state instead of staying stuck in find-Type"
Assert-NotContains $noteTypeAudio 'type audio ready rejected before BLE bond; opening pairing reset' `
    "unbonded Type heartbeat must not restart repair and refresh pairing forever"
Assert-NotContains $noteTypeAudio 'ble_hid_gap_forget_bonds_and_repair_inner\(true\)' `
    "Type heartbeat acceptance gate must not recursively call forget-bonds repair"
Assert-Contains $gap 'esp_err_t\s+esp_hid_ble_gap_adv_start\(void\)[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?NimBLE advertising deferred: recovery async local bond delete pending[\s\S]*?STATUS_LED_BLE_PAIRING' `
    "advertising must wait while recovery async local bond delete is pending"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_handle_connect_established[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?recovery: rejecting connection while async local bond delete is pending[\s\S]*?ble_gap_terminate\(conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "recovery must reject stale Windows connections while async local bond delete is pending"
Assert-Contains $gap 'refresh_pairing_window[\s\S]*?pairing window already active; refreshing advertising with stable BLE identity[\s\S]*?ble_hid_gap_open_recovery_pairing_window\([^)]*\)[\s\S]*?ble_hid_gap_start_advertising\(\)[\s\S]*?pairing window refreshed with stable BLE identity' `
    "explicit recovery during an already-open pairing window must renew the 120s pairing window and refresh advertising with the current stable identity"
Assert-Contains $gap 'BLE_HID_GAP_RANDOM_IDENTITY_KEY[\s\S]*?ble_hid_gap_restore_random_identity_from_nvs[\s\S]*?ble_hid_gap_rotate_native_recovery_identity[\s\S]*?ble_hs_id_gen_rnd\(0,\s*&addr\)' `
    "non-Type Windows-native recovery must rotate, persist, and restore a static-random BLE identity so stale host bonds cannot loop"
Assert-Contains $gap 's_own_addr_type\s*=\s*BLE_OWN_ADDR_RANDOM' `
    "native recovery random identity must advertise with BLE_OWN_ADDR_RANDOM after restore or rotation"
Assert-Contains $recoverySecurityHelper 's_recovery_security_request_conn_handle\s*=\s*conn_handle;[\s\S]*?recovery: waiting for Windows pairing security[\s\S]*?diag_log\(DIAG_SRC_BLE_GAP,\s*DIAG_GAP_RECOVERY,\s*DIAG_SEV_INFO,\s*10,\s*4,\s*0,\s*conn_handle\)' `
    "recovery security helper must mark the connection and wait for Windows PairAsync/native pairing security"
Assert-NotContains $recoverySecurityHelper 'ble_gap_security_initiate\(conn_handle\)' `
    "recovery security helper must not race Windows pairing by initiating Listener-side SMP during the pairing window"
Assert-Contains $gap 'case BLE_GAP_EVENT_CONNECT:[\s\S]*?ble_hid_gap_handle_connect_established\(event->connect\.conn_handle,\s*"adv_gap_connect"\)' `
    "advertising CONNECT callback must enter the shared connection helper"
Assert-Contains $gap 'static\s+int\s+ble_hid_gap_global_event_listener[\s\S]*?case BLE_GAP_EVENT_CONNECT:[\s\S]*?event->connect\.status\s*==\s*0[\s\S]*?ble_hid_gap_handle_connect_established\([\s\S]*?"global_gap_connect"\)' `
    "global GAP listener must also enter the shared connection helper"
Assert-Contains $gap 'else\s+if\s*\(conn_desc_valid\)[\s\S]*?ble_gap_security_initiate\(conn_handle\)[\s\S]*?security initiate requested conn=%u' `
    "normal Type audio connections must request security instead of accepting an unstable unpaired GATT session"
Assert-Contains $gap 'case BLE_GAP_EVENT_SUBSCRIBE:[\s\S]*?audio notify subscribed[\s\S]*?ble_hid_gap_close_recovery_for_type_audio\([\s\S]*?ble_hid_gap_request_recovery_security_once\(event->subscribe\.conn_handle,\s*"subscribe"\);[\s\S]*?case BLE_GAP_EVENT_MTU:' `
    "recovery subscribe must keep an idempotent secure-state check after Windows subscribes"
Assert-Contains $gap 'case BLE_GAP_EVENT_MTU:[\s\S]*?ble_hid_gap_request_recovery_security_once\(event->mtu\.conn_handle,\s*"mtu"\);[\s\S]*?case BLE_GAP_EVENT_ENC_CHANGE:' `
    "recovery MTU must keep an idempotent secure-state check for Windows pairing"
Assert-Contains $gap 'case BLE_GAP_EVENT_PASSKEY_ACTION:[\s\S]*?passkey action event[\s\S]*?BLE_SM_IOACT_NUMCMP[\s\S]*?passkey numeric comparison auto-accepted[\s\S]*?BLE_SM_IOACT_DISP[\s\S]*?BLE_SM_IOACT_INPUT[\s\S]*?BLE_HID_GAP_PAIRING_PASSKEY[\s\S]*?ble_sm_inject_io' `
    "GAP must log passkey actions, accept numeric comparison, and satisfy fallback passkey actions if Windows requests them"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?desc\.sec_state\.encrypted\s*\|\|\s*desc\.sec_state\.bonded[\s\S]*?ble_hid_gap_note_secure_connection\([\s\S]*?"secure connection established"\);' `
    "successful recovery pairing must switch from pairing LED to connected find-Type double flash"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?encryption failed or connection already gone[\s\S]*?ble_hid_gap_recovery_pairing_window_open\(\)[\s\S]*?pairing encryption failure[\s\S]*?keeping pairing advertising available for Windows retry' `
    "recovery pairing window must keep pairing discoverability available while Windows retries after encryption failure"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?pairing encryption failure status=%d; keeping pairing advertising available for Windows retry without restarting repair[\s\S]*?s_recovery_security_request_conn_handle\s*=\s*BLE_HS_CONN_HANDLE_NONE[\s\S]*?!ble_gap_adv_active\(\)[\s\S]*?ble_hid_gap_start_advertising\(\)' `
    "recovery pairing encryption failure must keep advertising available without recursively restarting repair"
Assert-NotContains $gap 'pairing encryption failure[\s\S]*?restarting pairing reset' `
    "recovery pairing encryption failure must not restart pairing reset"
Assert-Contains $pairingContract 'DENZIC_BLE_PAIRING_V1_SECURITY_FAILURE_WAIT_FOR_EXPLICIT_RECOVERY' `
    "shared pairing policy must expose the quiet out-of-window security-failure decision"
Assert-Contains $gap 'security failure outside pairing window status=%d; retaining bond and waiting for explicit recovery[\s\S]*?ble_hid_gap_set_explicit_recovery_required_after_security_failure\(true\);[\s\S]*?ble_hid_gap_explicit_recovery_led_state\(\)[\s\S]*?ble_gap_terminate\(' `
    "out-of-window encryption failure must retain the bond and wait without advertising for explicit recovery"
Assert-Contains $gap 'security failure outside pairing window status=%d; retaining bond and waiting for explicit recovery[\s\S]*?s_last_disconnect_host_deliberate\s*=\s*true;[\s\S]*?ble_hid_gap_set_explicit_recovery_required_after_security_failure\(true\);[\s\S]*?ble_hid_gap_explicit_recovery_led_state\(\)' `
    "lost host bond via encryption failure must select the same pairing-search LED state as Windows manual delete"
Assert-NotContains $gap 'security failure outside pairing window[\s\S]*?ble_hid_gap_forget_bonds_and_repair_inner\([\s\S]*?case BLE_GAP_EVENT_NOTIFY_TX:' `
    "manual Windows bond loss must not open a pairing reset from the encryption-failure callback"
Assert-Contains $gap 'ble_hid_gap_handle_disconnect\([^)]*\)[\s\S]*?ble_hid_gap_explicit_recovery_required_after_security_failure\(\)[\s\S]*?ble_hid_gap_explicit_recovery_led_state\(\)[\s\S]*?advertising remains off until explicit EC11/Type recovery[\s\S]*?return;' `
    "security-failed disconnect must preserve the explicit-hold LED state and suppress automatic advertising"
Assert-Contains $gap 'static\s+status_led_ble_state_t\s+ble_hid_gap_explicit_recovery_led_state\(void\)[\s\S]*?s_last_disconnect_host_deliberate[\s\S]*?STATUS_LED_BLE_PAIRING[\s\S]*?STATUS_LED_BLE_DISCONNECTED' `
    "manual deletion must use pairing attention while other explicit recovery holds remain dark"
Assert-Contains $gap 'host_deliberate_disconnect[\s\S]*?s_directed_adv_pending\s*=\s*false;[\s\S]*?restarting undirected connectable advertising while retaining the bond' `
    "host-terminated disconnect must avoid directed chasing while remaining connectable"
Assert-Contains $gap 'bool\s+ble_hid_gap_is_waiting_for_explicit_recovery\(void\)[\s\S]*?ble_hid_gap_explicit_recovery_required_after_security_failure\(\)' `
    "HID callbacks must be able to read the authoritative explicit-recovery hold"
Assert-Contains $gap 'bool\s+ble_hid_gap_is_manual_unpair_search_active\(void\)[\s\S]*?ble_hid_gap_explicit_recovery_required_after_security_failure\(\)[\s\S]*?s_last_disconnect_host_deliberate' `
    "HID callbacks must be able to distinguish manual-delete search from other explicit holds"
Assert-Contains $hid 'case\s+ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?ble_hid_gap_is_waiting_for_explicit_recovery\(\)[\s\S]*?ble_hid_gap_is_manual_unpair_search_active\(\)[\s\S]*?STATUS_LED_BLE_PAIRING[\s\S]*?STATUS_LED_BLE_DISCONNECTED[\s\S]*?STATUS_LED_BLE_RECONNECTING' `
    "the duplicate HID disconnect callback must retain pairing attention for manual delete, dark for other holds, and reconnecting otherwise"
Assert-Contains $gap 'ble_hid_gap_forget_bonds_and_repair_ec11_fast_inner\(void\)[\s\S]*?const bool suppress_native_swift_pair\s*=\s*false;[\s\S]*?const bool type_controlled_recovery\s*=\s*false;[\s\S]*?ble_hid_gap_forget_bonds_and_repair_inner\(\s*false,\s*suppress_native_swift_pair,\s*true\)[\s\S]*?ble_hid_gap_open_recovery_pairing_window\(\s*type_controlled_recovery,\s*suppress_native_swift_pair\)' `
    "physical EC11 recovery must rotate identity and expose native Swift Pair without Type ownership"
Assert-Contains $gap 'ble_hid_gap_start_advertising\(void\)[\s\S]*?ble_hid_gap_explicit_recovery_required_after_security_failure\(\)[\s\S]*?!ble_hid_gap_recovery_pairing_window_open\(\)[\s\S]*?advertising suppressed while security-failed bond waits for explicit recovery[\s\S]*?return ESP_OK;' `
    "all background advertising callers must respect the explicit-recovery hold"
Assert-Contains $gap 'case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?ble_hid_gap_recovery_bond_delete_active\(\)[\s\S]*?ble_gap_terminate\(event->enc_change\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "ENC_CHANGE may terminate only stale encrypted connections that arrive while async local bond delete is pending"
Assert-Contains $gap 'static\s+uint16_t\s+s_recovery_security_failed_conn_handle[\s\S]*?recovery: security skipped reason=%s conn=%u after prior encryption failure[\s\S]*?case BLE_GAP_EVENT_ENC_CHANGE:[\s\S]*?encryption failed or connection already gone[\s\S]*?s_recovery_security_failed_conn_handle\s*=\s*event->enc_change\.conn_handle[\s\S]*?ble_gap_terminate\(event->enc_change\.conn_handle,\s*BLE_ERR_REM_USER_CONN_TERM\)' `
    "recovery encryption failure must quarantine and terminate the failed pairing connection while leaving the pairing window available for a clean retry"
Assert-NotContains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "HID connect event must not mark an unauthenticated Windows link as connected LED"
Assert-Contains $hid 'static bool s_hid_control_suspended;' `
    "HID control suspend must be tracked separately from BLE connection state"
Assert-Contains $hid 'static bool ble_hid_usage_transport_ready\(void\)[\s\S]*?!s_usage_transport_test_blocked[\s\S]*?s_ble_connected[\s\S]*?!s_hid_control_suspended[\s\S]*?esp_hidd_dev_connected' `
    "HID suspend must pause HID usage dispatch without disabling USB serial command handling"
Assert-Contains $hid 'ESP_HIDD_CONTROL_EVENT:[\s\S]*?s_hid_control_suspended\s*=\s*!param->control\.control;[\s\S]*?if\s*\(param->control\.control\)[\s\S]*?ble_hid_task_start\(\);[\s\S]*?ble_hid_usage_task_start\(\);[\s\S]*?ble_hid_signal_usage_task\(\);[\s\S]*?else\s*\{[\s\S]*?HID suspended; keeping USB serial command task active' `
    "Windows HID suspend must keep the USB serial command task active for DEVICE/POWER/LED diagnostics"
Assert-Contains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?s_hid_control_suspended\s*=\s*false;' `
    "HID connect must clear stale suspend state"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?s_hid_control_suspended\s*=\s*false;' `
    "HID disconnect must clear suspend state"
Assert-NotContains $hid 'ble_hid_task_stop' `
    "HID suspend must never stop the shared USB serial command task"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?ble_hid_gap_is_waiting_for_explicit_recovery\(\)[\s\S]*?STATUS_LED_BLE_DISCONNECTED[\s\S]*?STATUS_LED_BLE_RECONNECTING' `
    "HID disconnect event must retain dark explicit recovery while ordinary drops still drive reconnecting LED"
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
Assert-Contains $audio 'BLE_AUDIO_STREAM_TYPE_LED_READY_HOLD_MS\s+12000' `
    "Type LED readiness must clear stale Type-exit cues quickly while still covering one missed 8s heartbeat"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_transport_state_type_ready\([^)]*\)[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING[\s\S]*?BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING' `
    "Type-link helper must remain true during active streaming/draining"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_link_ready\(void\)[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_transport_state_type_ready\(s_transport_state\)[\s\S]*?ble_audio_stream_type_heartbeat_recent\(\)' `
    "public Type-link readiness must combine notify link, streaming-capable states, and fresh Listener-Type heartbeat"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_type_led_link_ready\(void\)[\s\S]*?conn_handle\s*!=\s*BLE_HS_CONN_HANDLE_NONE' `
    "Type LED connected cue must require a real BLE connection"
Assert-Contains $audio 'bool\s+ble_audio_stream_is_type_led_ready\(void\)[\s\S]*?ble_audio_stream_transport_link_ready\(\)[\s\S]*?ble_audio_stream_type_heartbeat_led_recent\(\)' `
    "public Type-ready LED readiness must require the notify link plus heartbeat hold, so control-only heartbeats cannot look connected to Type"
Assert-Contains $audio 'static\s+void\s+ble_audio_stream_sync_power_manager_for_type_link\(bool active,[\s\S]*?if\s*\(active\)[\s\S]*?!ble_audio_stream_is_type_link_ready\(\)[\s\S]*?type link power sync skipped until notify link ready[\s\S]*?return;[\s\S]*?power_manager_set_ble_connected\(true\);[\s\S]*?!ble_audio_stream_hid_secure_connected\(\)[\s\S]*?power_manager_set_ble_connected\(false\);' `
    "accepted Type heartbeat must set power-manager BLE connected true only after notify readiness and only clear it when HID secure connection is absent"
Assert-Contains $audio 'static\s+bool\s+ble_audio_stream_type_activity_accepts_link\([^)]*\)[\s\S]*?ble_hid_gap_note_type_audio_ready\([^)]*\)[\s\S]*?type heartbeat rejected until secure BLE pairing completes[\s\S]*?return false;[\s\S]*?ble_hid_gap_is_recovery_pairing_window_open\(\)[\s\S]*?!ble_audio_stream_hid_secure_connected\(\)[\s\S]*?keeping Type link inactive' `
    "Type heartbeats must be rejected until GAP confirms secure pairing, so unbonded transient GATT cannot promote the power manager or BLE LED to connected"
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
Assert-Contains $audio 'void\s+ble_audio_stream_note_type_activity\([^)]*\)[\s\S]*?!ble_audio_stream_type_activity_accepts_link\(reason\)[\s\S]*?return;[\s\S]*?ble_audio_stream_note_type_host_seen\(reason\);[\s\S]*?ble_audio_stream_set_type_heartbeat_active\(\s*true[\s\S]*?ble_audio_stream_sync_power_manager_for_type_link\(\s*true[\s\S]*?ble_audio_stream_sync_status_led_for_type_link' `
    "Type activity helper must wait for secure GAP acceptance before refreshing host recency, heartbeat, and power-manager state"
Assert-Contains $gap 'ble_hid_gap_forget_bonds_and_repair_ec11_fast_inner\([^)]*\)[\s\S]*?const bool type_controlled_recovery\s*=\s*false;' `
    "physical EC11 recovery must always yield ownership to native Windows pairing"
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
Assert-Contains $statusLed 'STATUS_LED_BLE_RECONNECT_PULSE_PERCENT\s+STATUS_LED_BLE_ATTENTION_PERCENT' `
    "ordinary reconnecting must keep the Type-capped blue pulse peak"
Assert-Contains $statusLed 'case STATUS_LED_BLE_RECONNECTING:[\s\S]*?status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?STATUS_LED_BLE_RECONNECT_PULSE_PERCENT[\s\S]*?:\s*0U[\s\S]*?status_led_token_relative_to_peak_locked\(\s*ble_blue,\s*percent,\s*STATUS_LED_BLE_RECONNECT_PULSE_PERCENT,\s*false\s*\)[\s\S]*?break;' `
    "active reconnecting must render a blue double flash with a dark off phase; low blue floor belongs only to connected BLE states"
Assert-NotContains $statusLed 'case STATUS_LED_BLE_RECONNECTING:\s*\{(?:(?!break;)[\s\S])*?status_led_blink_on' `
    "ordinary reconnecting must use the dark-phase double pulse, not pairing-style blink"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s+2000U' `
    "HID-only connected must use a bounded find-Type double-flash period"
Assert-NotContains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_WINDOW_MS' `
    "active HID-only connected find-Type cue must not expire to dark from a short status window; only idle may turn BLE dark"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT\s+5U' `
    "HID-only connected find-Type cue must keep the operator-tuned low blue floor"
Assert-NotContains $statusLed 'STATUS_LED_BLE_DISCONNECTED_ACTIVE_PERCENT' `
    "disconnected/no-host must not keep a stale active-percent constant tied to the connected low floor"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT\s+STATUS_LED_BLE_ATTENTION_PERCENT' `
    "HID-only connected find-Type pulse must share the attention peak"
Assert-Contains $statusLed 'static\s+bool\s+status_led_connected_find_type_window_active_locked\(uint32_t now_ms\)[\s\S]*?\(void\)now_ms;[\s\S]*?return\s+true;' `
    "active HID-only connected find-Type cue must remain visible until Type-ready, disconnect, or idle"
Assert-Contains $statusLed 'static\s+bool\s+status_led_timed_output_active_locked\(uint32_t now_ms\)[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_PAIRING[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_REPAIRING[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_RECONNECTING[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_CONNECTED[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_TYPE_READY[\s\S]*?s_state\.ble_state == STATUS_LED_BLE_DISCONNECTED[\s\S]*?return true;' `
    "active BLE states must keep normal refresh until idle, independent of external power"
Assert-Contains $statusLed 'static\s+uint32_t\s+status_led_ble_recovery_window_elapsed_locked\(uint32_t now_ms\)[\s\S]*?s_state\.ble_repair_cue_until_ms != 0U[\s\S]*?return now_ms - s_state\.ble_repair_cue_until_ms;[\s\S]*?return status_led_ble_elapsed_locked\(now_ms\);' `
    "recovery-window double flash must be phase-anchored after the repair cue, not reset by BLE state transitions"
Assert-Contains $statusLed 'status_led_ble_recovery_window_active_locked\(now_ms\)[\s\S]*?const uint32_t recovery_elapsed_ms =[\s\S]*?status_led_ble_recovery_window_elapsed_locked\(now_ms\);[\s\S]*?recovery_elapsed_ms >= STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS[\s\S]*?status_led_double_pulse_on\(\s*recovery_elapsed_ms - STATUS_LED_BLE_REPAIR_TO_RECOVERY_GAP_MS,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?STATUS_LED_BLE_RECONNECT_PULSE_PERCENT[\s\S]*?:\s*0U' `
    "recovery-window reconnect cue must wait through the post-cue dark gap before using the anchored recovery phase"
Assert-Contains $statusLed 'const bool keep_repair_window = status_led_ble_repair_active_locked\(now_ms\)[\s\S]*?state == STATUS_LED_BLE_PAIRING[\s\S]*?state == STATUS_LED_BLE_RECONNECTING[\s\S]*?state == STATUS_LED_BLE_MANUAL_PAIRING[\s\S]*?if \(!keep_repair_cue && !keep_repair_window\) \{[\s\S]*?s_state\.ble_repair_cue_started_ms = 0U;[\s\S]*?s_state\.ble_repair_cue_until_ms = 0U;' `
    "BLE state transitions during recovery must not clear the active repair cue/window timing anchor"
Assert-NotContains $statusLed '!s_state\.external_power_present\s*&&\s*s_state\.ble_state != STATUS_LED_BLE_DISCONNECTED' `
    "active BLE refresh must not be limited to unplugged power; BLE may go dark only after idle"
Assert-Contains $statusLed 'case STATUS_LED_BLE_CONNECTED:\s*if\s*\(\s*status_led_connected_find_type_window_active_locked\(now_ms\)\s*\)\s*\{[\s\S]*?status_led_double_pulse_on\(\s*ble_elapsed_ms,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_PERIOD_MS\s*\)[\s\S]*?STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT[\s\S]*?STATUS_LED_BLE_CONNECTED_FIND_TYPE_MIN_PERCENT[\s\S]*?status_led_token_relative_to_peak_locked\(\s*ble_blue,\s*percent,\s*STATUS_LED_BLE_CONNECTED_FIND_TYPE_MAX_PERCENT,\s*false\s*\)[\s\S]*?break;\s*case STATUS_LED_BLE_TYPE_READY:\s*\{[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT' `
    "HID-only connected must show a continuous low-floor blue find-Type double flash while active, while Type-proven links stay steady blue"
Assert-Contains $statusLed 'case STATUS_LED_BLE_DISCONNECTED:\s*\{[\s\S]*?\(status_led_rgb_t\)\{0\}[\s\S]*?break;' `
    "active disconnected/no-host must stay dark; low blue floor belongs only to connected BLE states"
Assert-NotContains $statusLed 'status_led_connected_hid_only_percent_locked|STATUS_LED_BLE_CONNECTED_HEARTBEAT_PERIOD_MS|STATUS_LED_BLE_CONNECTED_CONFIRM_MS|STATUS_LED_BLE_CONNECTED_BASE_PERCENT' `
    "HID-only connected must not keep the old low-base heartbeat/confirmation renderer"
Assert-NotContains $statusLed 'status_led_low_power_ble_ready_window_active_locked' `
    "low-power idle must not retain the obsolete connected/TYPE_READY visibility window"
Assert-Contains $statusLed 'static\s+uint8_t\s+status_led_low_power_ble_percent_locked\(uint32_t now_ms,\s*uint32_t ble_elapsed_ms\)[\s\S]*?case STATUS_LED_BLE_PAIRING:[\s\S]*?STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS[\s\S]*?STATUS_LED_BLE_ATTENTION_PERCENT[\s\S]*?case STATUS_LED_BLE_RECONNECTING:\s*\n\s*return 0U;[\s\S]*?case STATUS_LED_BLE_CONNECTED:\s*case STATUS_LED_BLE_TYPE_READY:\s*return 0U;' `
    "low-power BLE must keep recovery pairing visible but render reconnecting, connected, and TYPE_READY dark"
Assert-Contains $statusLed 'static\s+uint8_t\s+status_led_low_power_ble_peak_percent_locked\(void\)[\s\S]*?STATUS_LED_BLE_PAIRING_PULSE_PERCENT[\s\S]*?STATUS_LED_BLE_ATTENTION_PERCENT[\s\S]*?status_led_render_low_power_ble_locked[\s\S]*?status_led_token_relative_to_peak_locked\(\s*status_led_rgb\(0,\s*0,\s*255\),\s*percent,\s*peak,\s*false\s*\)' `
    "low-power recovery BLE cues must use the same Type-capped design peaks as active BLE"
Assert-Contains $statusLed 'static\s+uint32_t\s+status_led_refresh_delay_ms_locked\(uint32_t now_ms\)[\s\S]*?if\s*\(\s*s_state\.low_power_disabled\s*\)\s*\{[\s\S]*?STATUS_LED_EXTERNAL_POWER_POLL_MS[\s\S]*?STATUS_LED_LOW_POWER_IDLE_REFRESH_MS;' `
    "low-power idle must use bounded external-power or battery refresh cadence"
Assert-Contains $statusLed 'const bool active_work = s_state\.recording_active \|\| s_state\.processing_active \|\| s_state\.ota_active;' `
    "status LED renderer must define active work for recording/processing/OTA visibility"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT\s+14U[\s\S]*?percent = \(status_window \|\| active_work\)[\s\S]*?\? STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT[\s\S]*?: STATUS_LED_BATTERY_STATUS_WINDOW_PWR_PERCENT;' `
    "battery PWR active-rendering and idle fallback must both use the Type-capped status-window brightness"
Assert-Contains $statusDoc '30 second Type-ready hold' `
    "status LED documentation must describe the active Type-ready LED hold"
Assert-Contains $statusDoc 'If a recent Type heartbeat or Type-host presence window proves Listener-Type was active on this computer[\s\S]*?If Type is absent and no recent Type-host presence exists, native recovery rotates and persists a new static-random BLE identity[\s\S]*?create a new bond through the recovery window' `
    "status LED documentation must describe Type-present stable recovery, no-Type native identity rotation, and async local bond-delete semantics"

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

if (-not [string]::IsNullOrWhiteSpace($ReadbackLog)) {
    $readbackPath = if ([System.IO.Path]::IsPathRooted($ReadbackLog)) {
        $ReadbackLog
    } else {
        Join-Path $RepoRoot $ReadbackLog
    }
    if (-not (Test-Path -LiteralPath $readbackPath)) {
        throw "verify_ble_status_led_connected_sync failed: missing readback log $ReadbackLog"
    }
    $readback = Get-Content -LiteralPath $readbackPath -Raw
    foreach ($token in @(
        "ble=type_ready",
        "active_flags=PWR:1,BLE:1,REC:0,AI:0,OK:0,WARN:0,EC11:0,KEY:0,EDGE:0",
        "led_status=80 led_key=80 led_ec11=80 led_edge=80",
        "status_zone_brightness_percent=80 key_zone_brightness_percent=80 ec11_zone_brightness_percent=80 edge_zone_brightness_percent=80",
        "spi_dma_actual=status:0,ec11:1,key:1,edge:0",
        "rmt_tx_dma_actual=status:1,ec11:0,key:0,edge:0",
        "ble_repair_ms_left=0 ble_repair_cue_ms_left=0",
        "key_rgb=px1:0,0,0;px2:0,0,0;px3:0,0,0;px4:0,0,0",
        "ec11_rgb=px1:0,0,0;px2:0,0,0;px3:0,0,0;px4:0,0,0;px5:0,0,0;px6:0,0,0;px7:0,0,0;px8:0,0,0;px9:0,0,0;px10:0,0,0;px11:0,0,0;px12:0,0,0",
        "edge_rgb=px1:0,0,0;px2:0,0,0;px3:0,0,0;px4:0,0,0;px5:0,0,0;px6:0,0,0"
    )) {
        Assert-ReadbackContains $readback $token "current BLE LED readback lost expected token: $token"
    }
}

Write-Host "PASS: BLE status LED connected-sync checks cover GAP/HID connected source of truth, stable-identity re-pair advertising, host-terminated undirected reconnect, audio-stream TYPE_READY sync with LED hold, stale advertising suppression, connected battery resync after preview clears, pairing/reconnect dark-phase pulses, HID-only connected low-floor find-Type, disconnected/no-host dark, steady TYPE_READY brightness, idle BLE dark, active-work PWR/OTA visibility, and disconnect/advertising negative transitions."
