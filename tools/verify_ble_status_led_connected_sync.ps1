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
Assert-Contains $gap 'static\s+status_led_ble_state_t\s+ble_hid_gap_status_led_connected_state\(void\)[\s\S]*?ble_audio_stream_is_ready\(\)[\s\S]*?STATUS_LED_BLE_TYPE_READY[\s\S]*?STATUS_LED_BLE_CONNECTED;' `
    "GAP connected LED helper must promote only audio-ready Type sessions to type_ready"
Assert-Contains $gap 'static\s+void\s+ble_hid_gap_refresh_connected_status_led\(bool\s+confidence_window\)[\s\S]*?ble_hid_gap_connection_snapshot\(\)[\s\S]*?status_led_set_ble_state\(state,\s*confidence_window\);' `
    "GAP connected LED refresh must use the shared connection snapshot and chosen connected/type_ready state"
Assert-Contains $gap 'if\s*\(\s*conn\.connected\s*\)\s*\{[\s\S]*?NimBLE advertising skipped: GAP already connected[\s\S]*?ble_hid_gap_refresh_connected_status_led\(false\);[\s\S]*?return ESP_OK;' `
    "GAP advertising must skip stale advertising while connected and restore the connected/type-ready LED"
Assert-Order $advStart 'if (conn.connected)' 'if (ble_gap_adv_active())' `
    "connected guard must run before advertising-active handling"
Assert-Order $advStart 'if (conn.connected)' 'status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);' `
    "connected guard must run before directed advertising can set reconnecting LED"
Assert-Order $advStart 'if (conn.connected)' 'status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);' `
    "connected guard must run before undirected advertising can set pairing LED"
Assert-Contains $gap 'ble_hid_gap_set_connection_state\(true,\s*event->connect\.conn_handle\);[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "GAP connect event must mark connected before refreshing connected LED"
Assert-Contains $gap 'ble_audio_stream_on_gap_connect\(event->connect\.conn_handle\);[\s\S]*?ble_hid_gap_refresh_connected_status_led\(false\);' `
    "GAP connect event must refresh the LED after audio transport observes the connection"
Assert-Contains $gap 'ble_audio_stream_on_gap_subscribe\([\s\S]*?event->subscribe\.cur_indicate\);[\s\S]*?ble_hid_gap_refresh_connected_status_led\(false\);' `
    "GAP subscribe event must promote the LED when Type enables audio notify"
Assert-Contains $gap 'ble_audio_stream_on_gap_mtu\(event->mtu\.conn_handle,\s*event->mtu\.value\);[\s\S]*?ble_hid_gap_refresh_connected_status_led\(false\);' `
    "GAP MTU event must refresh the Type-ready LED after the transport becomes ready"
Assert-Contains $gap 'ble_hid_gap_set_connection_state\(false,\s*BLE_HS_CONN_HANDLE_NONE\);[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "GAP disconnect event must clear connected before reconnecting LED"
Assert-Contains $hid 'ESP_HIDD_CONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*true;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_CONNECTED,\s*true\);' `
    "HID connect event must refresh connected LED and confidence window"
Assert-Contains $hid 'ESP_HIDD_DISCONNECT_EVENT:[\s\S]*?s_ble_connected\s*=\s*false;[\s\S]*?status_led_set_ble_state\(STATUS_LED_BLE_RECONNECTING,\s*false\);' `
    "HID disconnect event must still drive reconnecting LED"
Assert-Contains $statusLed 'STATUS_LED_STATUS_WINDOW_MS\s+6000U' `
    "connected status window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONFIDENCE_MS\s+8000U' `
    "connected BLE confidence window must remain bounded"
Assert-Contains $statusLed 'STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS\s+120U' `
    "battery idle BLE heartbeat must be brief"
Assert-Contains $statusLed 'STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT\s+38U[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT\s+STATUS_LED_BLE_CONNECTED_STEADY_PERCENT' `
    "status LED must distinguish generic BLE connected from Type-ready brightness"
Assert-Contains $statusLed 'case STATUS_LED_BLE_CONNECTED:[\s\S]*?case STATUS_LED_BLE_TYPE_READY:[\s\S]*?STATUS_LED_BLE_CONNECTED_GENERIC_PERCENT[\s\S]*?STATUS_LED_BLE_TYPE_READY_STEADY_PERCENT[\s\S]*?STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT' `
    "connected rendering must keep Type-ready distinct while preserving battery idle heartbeat"
Assert-Contains $statusLed 'if \(changed && status_led_ble_state_ready_locked\(state\) && confidence_window\)' `
    "connected confidence window must apply to both generic connected and Type-ready states"
Assert-Contains $statusLed 'const bool active_work = s_state\.recording_active \|\| s_state\.processing_active;' `
    "status LED renderer must define active work for recording/processing visibility"
Assert-Contains $statusLed 'percent = \(status_window \|\| active_work\) \? 46U : 0U;' `
    "battery PWR must stay readable during active recording/processing and turn off after idle status window"
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
        "type_ready" {
            if ($script:connected) {
                $script:modelState = "type_ready"
            }
        }
        "stale_adv_complete" {
            if ($script:connected) {
                if ($script:modelState -ne "type_ready") {
                    $script:modelState = "connected"
                }
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
Apply-ModelEvent "type_ready"
Apply-ModelEvent "stale_adv_complete"
if ($modelState -ne "type_ready") {
    throw "verify_ble_status_led_connected_sync failed: pairing-to-connected model regressed to $modelState"
}

Apply-ModelEvent "disconnect"
if ($modelState -ne "reconnecting") {
    throw "verify_ble_status_led_connected_sync failed: disconnect negative transition lost reconnecting state"
}

Apply-ModelEvent "directed_advertising"
Apply-ModelEvent "connect"
Apply-ModelEvent "type_ready"
Apply-ModelEvent "stale_adv_complete"
if ($modelState -ne "type_ready") {
    throw "verify_ble_status_led_connected_sync failed: reconnect-to-connected model regressed to $modelState"
}

Write-Host "PASS: BLE status LED connected-sync checks cover GAP/HID connected source of truth, Type audio notify readiness, stale advertising suppression, bounded connected/type-ready brightness, active-work PWR/BLE visibility, battery idle heartbeat, and disconnect/advertising negative transitions."
