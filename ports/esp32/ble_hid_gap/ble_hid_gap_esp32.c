/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * NimBLE-only BLE HID GAP layer.
 * Bluedroid (classic BT + legacy BLE) paths removed — this project uses
 * CONFIG_BT_NIMBLE_ENABLED exclusively.
 */


#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ble_hid_gap.h"
#include "ble_audio_stream.h"
#include "ble_firmware_ota.h"
#include "ble_diag_log.h"
#include "device_settings.h"
#include "diag_log.h"
#include "listener_device.h"
#include "power_manager.h"
#include "status_led.h"

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "host/ble_sm.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP_HID_GAP";

extern void ble_hid_task_start_up(void);

static int ble_hid_gap_get_bonded_peer_count(int *out_count);
static esp_err_t ble_hid_gap_forget_bonds_and_repair_inner(
    bool type_controlled_request,
    bool suppress_swift_pair_prompt);
static void ble_hid_gap_close_recovery_pairing_window(const char *reason);
static void ble_hid_gap_note_secure_connection(uint16_t conn_handle, const char *reason);
static bool ble_hid_gap_recovery_pairing_window_open(void);

static void ble_hid_gap_log_conn_desc(const char *context, uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "%s: connection descriptor lookup failed: conn_handle=%u rc=%d", context, conn_handle, rc);
        return;
    }

    ESP_LOGI(
        TAG,
        "%s: conn_handle=%u interval_units=%u interval_ms=%.2f latency=%u supervision_timeout_units=%u supervision_timeout_ms=%u",
        context,
        conn_handle,
        desc.conn_itvl,
        (double)desc.conn_itvl * 1.25,
        desc.conn_latency,
        desc.supervision_timeout,
        desc.supervision_timeout * 10);
}

/*
 * NimBLE GAP
 */

#define GATT_SVR_SVC_HID_UUID 0x1812
#define BLE_HID_GAP_FAST_ADV_MIN_MS 30U
#define BLE_HID_GAP_FAST_ADV_MAX_MS 50U
#define BLE_HID_GAP_ACTIVE_ITVL_MIN 6U
#define BLE_HID_GAP_ACTIVE_ITVL_MAX 6U
#define BLE_HID_GAP_ACTIVE_LATENCY 0U
#define BLE_HID_GAP_ACTIVE_SUPERVISION_TIMEOUT 800U
#define BLE_HID_GAP_LOW_POWER_ITVL_MIN 80U
#define BLE_HID_GAP_LOW_POWER_ITVL_MAX 120U
#define BLE_HID_GAP_LOW_POWER_LATENCY 9U
#define BLE_HID_GAP_LOW_POWER_SUPERVISION_TIMEOUT 600U
#define BLE_HID_GAP_PAIRING_PASSKEY 123456U
#define BLE_HID_GAP_CONN_PARAM_COLLISION_BACKOFF_MS 4200U
#define BLE_HID_GAP_ACTIVE_REQUEST_DEFER_MS 750U
#define BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS 50U
#define BLE_HID_GAP_ACTIVE_PROMOTION_TIMEOUT_MS \
    (BLE_HID_GAP_CONN_PARAM_COLLISION_BACKOFF_MS + 2000U)
#define BLE_HID_GAP_ACTIVE_REQUEST_TASK_STACK_BYTES 3072U
#define BLE_HID_GAP_OTA_RECONNECT_DEFER_MS 750U
#define BLE_HID_GAP_OTA_RECONNECT_TASK_STACK_BYTES 3072U

static struct ble_hs_adv_fields s_adv_fields;
static struct ble_hs_adv_fields s_scan_rsp_fields;
static ble_uuid16_t s_hid_service_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID);
static ble_uuid128_t s_audio_stream_service_uuid = BLE_AUDIO_STREAM_SERVICE_UUID;
static bool s_nimble_stack_ready = false;
static bool s_hid_start_event_seen = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static bool s_directed_adv_pending = true;
static bool s_last_adv_was_directed = false;
static bool s_ble_gap_connected = false;
static bool s_ble_gap_secure_connected = false;
static bool s_audio_enabled = true;
static bool s_low_power_advertising = false;
static bool s_key_wake_only_advertising = false;
static bool s_shutdown_quiesce = false;
static uint16_t s_ble_gap_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static portMUX_TYPE s_ble_gap_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_service_changed_val_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_service_changed_state_loaded = false;
static bool s_service_changed_pending = false;
static bool s_service_changed_queued_for_conn = false;
static char s_service_changed_schema_id[64];
static bool s_recovery_pairing_window_active = false;
static bool s_recovery_swift_pair_consumed = false;
static bool s_recovery_type_controlled_pairing = false;
static bool s_recovery_suppress_swift_pair_prompt = false;
static bool s_recovery_waiting_for_disconnect = false;
static bool s_recovery_power_blocker_active = false;
static bool s_recovery_bond_delete_pending = false;
static bool s_recovery_bond_delete_in_progress = false;
static TaskHandle_t s_recovery_bond_delete_task_handle = NULL;
static esp_timer_handle_t s_recovery_pairing_window_timer = NULL;
static int64_t s_recovery_pairing_window_opened_at_ms = 0;
static uint16_t s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static int64_t s_first_pairing_window_opened_at_ms = 0;
static uint32_t s_last_conn_param_mode = 0;
static TickType_t s_conn_param_retry_not_before_tick = 0;
static TickType_t s_conn_param_request_pending_until_tick = 0;
static bool s_active_connection_request_pending = false;
static bool s_active_connection_required = false;
static bool s_ec11_fast_recording_armed = false;
static bool s_ota_reconnect_request_pending = false;
static struct ble_gap_event_listener s_ble_hid_gap_event_listener;
static bool s_ble_hid_gap_event_listener_registered = false;
static uint16_t s_last_disconnect_event_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static int s_last_disconnect_event_reason = 0;
static int64_t s_last_disconnect_event_at_ms = 0;

typedef enum {
    BLE_HID_CONN_PARAM_MODE_ACTIVE = 1,
    BLE_HID_CONN_PARAM_MODE_LOW_POWER = 2,
} ble_hid_conn_param_mode_t;

static esp_err_t ble_hid_gap_schedule_active_connection_with_delay(
    uint32_t delay_ms,
    const char *reason);

static bool ble_hid_gap_conn_desc_matches_params(
    uint16_t conn_handle,
    uint16_t itvl_min,
    uint16_t itvl_max,
    uint16_t latency)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        return false;
    }

    return desc.conn_itvl >= itvl_min &&
           desc.conn_itvl <= itvl_max &&
           desc.conn_latency == latency;
}

static uint32_t ble_hid_gap_last_conn_param_mode(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    uint32_t mode = s_last_conn_param_mode;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return mode;
}

static void ble_hid_gap_set_active_connection_required(bool required)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_active_connection_required = required;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
}

static bool ble_hid_gap_active_connection_required(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    bool required = s_active_connection_required;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return required;
}

static bool ble_hid_gap_ec11_fast_recording_armed(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    bool armed = s_ec11_fast_recording_armed;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return armed;
}

static void ble_hid_gap_confirm_conn_param_update(uint16_t conn_handle)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    uint32_t confirmed_mode = s_last_conn_param_mode;
    s_last_conn_param_mode = 0;
    s_conn_param_retry_not_before_tick = 0;
    s_conn_param_request_pending_until_tick = 0;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    ESP_LOGI(TAG,
             "connection parameter update confirmed: conn=%u mode=%u",
             conn_handle,
             (unsigned)confirmed_mode);
}

static bool ble_hid_gap_tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static bool ble_hid_gap_conn_param_retry_is_deferred(void)
{
    TickType_t retry_not_before_tick = 0;
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    retry_not_before_tick = s_conn_param_retry_not_before_tick;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    return retry_not_before_tick != 0 &&
           !ble_hid_gap_tick_reached(xTaskGetTickCount(), retry_not_before_tick);
}

static bool ble_hid_gap_conn_param_request_is_pending(void)
{
    uint32_t requested_mode = 0;
    TickType_t pending_until_tick = 0;
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    requested_mode = s_last_conn_param_mode;
    pending_until_tick = s_conn_param_request_pending_until_tick;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    return requested_mode != 0 && pending_until_tick != 0;
}

static void ble_hid_gap_defer_conn_param_retry_after_collision(uint16_t conn_handle)
{
    TickType_t retry_not_before_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(BLE_HID_GAP_CONN_PARAM_COLLISION_BACKOFF_MS);
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_last_conn_param_mode = 0;
    s_conn_param_retry_not_before_tick = retry_not_before_tick;
    s_conn_param_request_pending_until_tick = 0;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    ESP_LOGW(
        TAG,
        "connection parameter update collided with central procedure; deferring shared retry for %u ms conn=%u",
        (unsigned)BLE_HID_GAP_CONN_PARAM_COLLISION_BACKOFF_MS,
        conn_handle);
}

static void ble_hid_gap_clear_conn_param_mode(const char *reason)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    uint32_t previous = s_last_conn_param_mode;
    s_last_conn_param_mode = 0;
    s_conn_param_retry_not_before_tick = 0;
    s_conn_param_request_pending_until_tick = 0;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    if (previous != 0) {
        ESP_LOGW(TAG, "connection parameter mode cache cleared after %s: previous_mode=%u",
                 reason != NULL ? reason : "unknown", (unsigned)previous);
    }
}

typedef struct {
    bool connected;
    bool secure_connected;
    uint16_t conn_handle;
} ble_hid_gap_connection_snapshot_t;

static ble_hid_gap_connection_snapshot_t ble_hid_gap_connection_snapshot(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    ble_hid_gap_connection_snapshot_t snapshot = {
        .connected = s_ble_gap_connected,
        .secure_connected = s_ble_gap_secure_connected,
        .conn_handle = s_ble_gap_conn_handle,
    };
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return snapshot;
}

static void ble_hid_gap_set_connection_state(bool connected, uint16_t conn_handle)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_ble_gap_connected = connected;
    if (!connected) {
        s_ble_gap_secure_connected = false;
    }
    s_ble_gap_conn_handle = conn_handle;
    s_last_conn_param_mode = 0;
    s_conn_param_retry_not_before_tick = 0;
    s_conn_param_request_pending_until_tick = 0;
    if (!connected) {
        s_active_connection_required = false;
    }
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
}

static void ble_hid_gap_set_secure_connection_state(bool secure_connected)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_ble_gap_secure_connected = secure_connected && s_ble_gap_connected;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
}

static ble_hid_gap_connection_snapshot_t ble_hid_gap_reconcile_connection_snapshot(const char *reason)
{
    ble_hid_gap_connection_snapshot_t snapshot = ble_hid_gap_connection_snapshot();
    if (!snapshot.connected) {
        return snapshot;
    }

    int rc = BLE_HS_ENOTCONN;
    if (snapshot.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc desc;
        rc = ble_gap_conn_find(snapshot.conn_handle, &desc);
        if (rc == 0) {
            return snapshot;
        }
    }

    ESP_LOGW(
        TAG,
        "clearing stale BLE GAP connection state: reason=%s conn=%u secure=%u rc=%d",
        reason != NULL ? reason : "unknown",
        snapshot.conn_handle,
        snapshot.secure_connected ? 1U : 0U,
        rc);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
             21, (uint32_t)rc, snapshot.secure_connected ? 1U : 0U, snapshot.conn_handle);

    ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
    s_service_changed_queued_for_conn = false;
    if (s_audio_enabled && snapshot.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_audio_stream_on_gap_disconnect(snapshot.conn_handle);
    }
    if (snapshot.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_diag_log_on_gap_disconnect(snapshot.conn_handle);
        ble_firmware_ota_on_gap_disconnect(snapshot.conn_handle);
    }
    power_manager_set_ble_connected(false);
    status_led_set_ble_state(
        ble_hid_gap_recovery_pairing_window_open()
            ? STATUS_LED_BLE_PAIRING
            : STATUS_LED_BLE_RECONNECTING,
        false);

    return ble_hid_gap_connection_snapshot();
}

static bool ble_hid_gap_recovery_bond_delete_active(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    bool active = s_recovery_bond_delete_pending || s_recovery_bond_delete_in_progress;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return active;
}

typedef enum {
    BLE_HID_GAP_ADV_STATE_SUPPRESS_SHUTDOWN = 1,
    BLE_HID_GAP_ADV_STATE_SUPPRESS_KEY_WAKE = 2,
    BLE_HID_GAP_ADV_STATE_DEFER_HID_START = 3,
    BLE_HID_GAP_ADV_STATE_DEFER_STACK_SYNC = 4,
    BLE_HID_GAP_ADV_STATE_SKIP_CONNECTED = 5,
    BLE_HID_GAP_ADV_STATE_ALREADY_ACTIVE = 6,
    BLE_HID_GAP_ADV_STATE_LOW_POWER_SET = 7,
    BLE_HID_GAP_ADV_STATE_KEY_WAKE_STOP = 8,
    BLE_HID_GAP_ADV_STATE_SHUTDOWN_PREPARE = 9,
    BLE_HID_GAP_ADV_STATE_RECONNECT_REQUEST = 10,
    BLE_HID_GAP_ADV_STATE_CONNECT_FAIL_SUPPRESS = 11,
    BLE_HID_GAP_ADV_STATE_DISCONNECT_SUPPRESS = 12,
    BLE_HID_GAP_ADV_STATE_ADV_COMPLETE_SUPPRESS = 13,
    BLE_HID_GAP_ADV_STATE_STOP_FOR_RESTART = 14,
    BLE_HID_GAP_ADV_STATE_STOP_FOR_SHUTDOWN = 15,
    BLE_HID_GAP_ADV_STATE_STOP_FOR_RECONNECT = 16,
    BLE_HID_GAP_ADV_STATE_START_DIRECTED = 17,
    BLE_HID_GAP_ADV_STATE_START_UNDIRECTED = 18,
    BLE_HID_GAP_ADV_STATE_DEFER_BOND_DELETE = 19,
} ble_hid_gap_adv_state_action_t;

#define BLE_HID_GAP_ADV_FLAG_ACTIVE          (1U << 0)
#define BLE_HID_GAP_ADV_FLAG_LOW_POWER       (1U << 1)
#define BLE_HID_GAP_ADV_FLAG_DIRECTED_PENDING (1U << 2)
#define BLE_HID_GAP_ADV_FLAG_KEY_WAKE_ONLY   (1U << 3)
#define BLE_HID_GAP_ADV_FLAG_SHUTDOWN_QUIESCE (1U << 4)
#define BLE_HID_GAP_ADV_FLAG_NIMBLE_READY    (1U << 5)
#define BLE_HID_GAP_ADV_FLAG_HID_STARTED     (1U << 6)
#define BLE_HID_GAP_ADV_FLAG_CONNECTED       (1U << 7)
#define BLE_HID_GAP_ADV_FLAG_BOND_DELETE     (1U << 8)

static bool ble_hid_gap_adv_active_snapshot(void)
{
    return s_nimble_stack_ready && ble_gap_adv_active();
}

static uint32_t ble_hid_gap_adv_state_flags(bool adv_active)
{
    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    return (adv_active ? BLE_HID_GAP_ADV_FLAG_ACTIVE : 0U) |
           (s_low_power_advertising ? BLE_HID_GAP_ADV_FLAG_LOW_POWER : 0U) |
           (s_directed_adv_pending ? BLE_HID_GAP_ADV_FLAG_DIRECTED_PENDING : 0U) |
           (s_key_wake_only_advertising ? BLE_HID_GAP_ADV_FLAG_KEY_WAKE_ONLY : 0U) |
           (s_shutdown_quiesce ? BLE_HID_GAP_ADV_FLAG_SHUTDOWN_QUIESCE : 0U) |
           (s_nimble_stack_ready ? BLE_HID_GAP_ADV_FLAG_NIMBLE_READY : 0U) |
           (s_hid_start_event_seen ? BLE_HID_GAP_ADV_FLAG_HID_STARTED : 0U) |
           (conn.connected ? BLE_HID_GAP_ADV_FLAG_CONNECTED : 0U) |
           (ble_hid_gap_recovery_bond_delete_active() ? BLE_HID_GAP_ADV_FLAG_BOND_DELETE : 0U);
}

static void ble_hid_gap_log_adv_state(
    ble_hid_gap_adv_state_action_t action,
    uint32_t result_or_detail,
    bool adv_active,
    uint16_t conn_handle,
    uint8_t severity)
{
    diag_log(DIAG_SRC_BLE_GAP,
             DIAG_GAP_ADV_STATE,
             severity,
             (uint32_t)action,
             result_or_detail,
             ble_hid_gap_adv_state_flags(adv_active),
             (uint32_t)conn_handle);
}

/*
 * Legacy advertising has a hard 31-byte payload limit. Normal advertising keeps
 * flags + appearance + one 16-bit HID UUID + a short local name. Swift Pair
 * support stays compiled in, but normal advertising keeps Swift Pair disabled
 * so Listener does not raise Windows' native "Connect" toast during ordinary
 * scanning. A recovery pairing window exposes a short Swift Pair payload because
 * Windows PairAsync can return DevicePairingResultStatus::Failed even while the
 * device remains pairable; the native prompt is the reliable fallback, and it is
 * bounded so it does not keep nagging.
 */
#define BLE_HID_ADV_NAME_MAX_LEN 17
#define BLE_HID_SCAN_RSP_NAME_MAX_LEN 29
#define BLE_HID_SWIFT_PAIR_MFG_DATA_LEN 5
#define BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_HID_UUID 13
#define BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_APPEARANCE 17
#define BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITHOUT_APPEARANCE 21
#define BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE "ble_gap"
#define BLE_HID_GAP_SERVICE_CHANGED_STATE_KEY "svcchg_fw"
#define BLE_HID_GAP_RANDOM_IDENTITY_KEY "rnd_id"
#define BLE_HID_GAP_GATT_SCHEMA_REV "denzic_ota_v1_uuid1"
#define BLE_HID_GAP_SERVICE_CHANGED_START_HANDLE 0x0001
#define BLE_HID_GAP_SERVICE_CHANGED_END_HANDLE 0xffff
#define BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS 120000LL
#define BLE_HID_GAP_RECOVERY_SWIFT_PAIR_PROMPT_MS 45000LL
#define BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS 0LL
#define BLE_HID_GAP_SWIFT_PAIR_ADV_MIN_RESTART_MS 1000LL
#define BLE_HID_GAP_RECOVERY_BOND_DELETE_SETTLE_MS 80U
#define BLE_HID_GAP_RECOVERY_BOND_DELETE_WAIT_MS 2000U
#define BLE_HID_GAP_RECOVERY_BOND_DELETE_POLL_MS 50U
#define BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_STACK 4096U
#define BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_PRIO 4U

static const uint8_t s_swift_pair_mfg_data[BLE_HID_SWIFT_PAIR_MFG_DATA_LEN] = {
    0x06, 0x00, /* Microsoft Bluetooth SIG company identifier, little-endian. */
    0x03,       /* Swift Pair beacon ID for BLE peripherals. */
    0x00,       /* BLE-only pairing scenario. */
    0x80,       /* Reserved RSSI byte: let Windows use measured RSSI. */
};
static uint8_t s_swift_pair_mfg_payload[
    BLE_HID_SWIFT_PAIR_MFG_DATA_LEN + BLE_HID_SCAN_RSP_NAME_MAX_LEN];
static const char *s_adv_device_name = NULL;
static size_t s_adv_device_name_len = 0;
static uint16_t s_adv_appearance = 0;
static bool s_native_recovery_identity_rotate_pending = false;
static bool s_native_recovery_random_identity_active = false;

static int64_t ble_hid_gap_now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static void ble_hid_gap_log_random_identity(
    const char *message,
    const char *reason,
    const uint8_t addr[6])
{
    ESP_LOGW(
        TAG,
        "%s reason=%s addr=%02x:%02x:%02x:%02x:%02x:%02x",
        message,
        reason != NULL ? reason : "unknown",
        addr[0],
        addr[1],
        addr[2],
        addr[3],
        addr[4],
        addr[5]);
}

static esp_err_t ble_hid_gap_store_random_identity(const uint8_t addr[6])
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(nvs, BLE_HID_GAP_RANDOM_IDENTITY_KEY, addr, 6);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

static esp_err_t ble_hid_gap_load_random_identity(uint8_t addr[6])
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READONLY,
        &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t len = 6;
    ret = nvs_get_blob(nvs, BLE_HID_GAP_RANDOM_IDENTITY_KEY, addr, &len);
    nvs_close(nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    return len == 6 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t ble_hid_gap_apply_random_identity(
    const uint8_t addr[6],
    const char *reason,
    bool persist)
{
    int rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        ESP_LOGE(
            TAG,
            "BLE random identity set failed reason=%s rc=%d",
            reason != NULL ? reason : "unknown",
            rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 5, (uint32_t)rc, 0, s_ble_gap_conn_handle);
        return ESP_FAIL;
    }

    s_own_addr_type = BLE_OWN_ADDR_RANDOM;
    s_native_recovery_random_identity_active = true;
    s_native_recovery_identity_rotate_pending = false;

    if (persist) {
        esp_err_t store_ret = ble_hid_gap_store_random_identity(addr);
        if (store_ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "BLE random identity persist failed reason=%s ret=%s; pairing can continue but reboot would need recovery again",
                reason != NULL ? reason : "unknown",
                esp_err_to_name(store_ret));
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     5, (uint32_t)store_ret, 2, s_ble_gap_conn_handle);
        }
    }

    ble_hid_gap_log_random_identity(
        "recovery: native Windows pairing using BLE random identity",
        reason,
        addr);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             5, 0, persist ? 1U : 0U, s_ble_gap_conn_handle);
    return ESP_OK;
}

static esp_err_t ble_hid_gap_restore_random_identity_from_nvs(void)
{
    uint8_t addr[6] = {0};
    esp_err_t ret = ble_hid_gap_load_random_identity(addr);
    if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_NOT_INITIALIZED) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE random identity load skipped: %s", esp_err_to_name(ret));
        return ESP_OK;
    }
    return ble_hid_gap_apply_random_identity(addr, "nimble_sync_restore", false);
}

static void ble_hid_gap_defer_native_recovery_identity_rotation(const char *reason)
{
    s_native_recovery_identity_rotate_pending = true;
    ESP_LOGW(
        TAG,
        "recovery: native Windows pairing will rotate BLE identity after disconnect reason=%s",
        reason != NULL ? reason : "unknown");
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
             5, 1, 0, s_ble_gap_conn_handle);
}

static esp_err_t ble_hid_gap_rotate_native_recovery_identity(const char *reason)
{
    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_hid_gap_defer_native_recovery_identity_rotation(reason);
        return ESP_OK;
    }

    if (ble_gap_adv_active()) {
        int stop_rc = ble_gap_adv_stop();
        if (stop_rc != 0) {
            ESP_LOGW(
                TAG,
                "recovery: advertising stop before native identity rotation failed rc=%d; rotation deferred",
                stop_rc);
            s_native_recovery_identity_rotate_pending = true;
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     5, (uint32_t)stop_rc, 3, s_ble_gap_conn_handle);
            return ESP_OK;
        }
    }

    ble_addr_t addr = {0};
    int rc = ble_hs_id_gen_rnd(0, &addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "recovery: BLE random identity generation failed rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 5, (uint32_t)rc, 4, s_ble_gap_conn_handle);
        return ESP_FAIL;
    }

    return ble_hid_gap_apply_random_identity(
        addr.val,
        reason != NULL ? reason : "native_recovery_pairing",
        true);
}

static bool ble_hid_gap_adv_start_deferred_rc(int rc)
{
    return rc == BLE_HS_EALREADY ||
           rc == BLE_HS_EBUSY ||
           rc == BLE_HS_HCI_ERR(BLE_ERR_CMD_DISALLOWED);
}

static int64_t ble_hid_gap_window_remaining_ms(
    int64_t opened_at_ms,
    int64_t window_ms)
{
    if (opened_at_ms <= 0 || window_ms <= 0) {
        return 0;
    }

    int64_t elapsed_ms = ble_hid_gap_now_ms() - opened_at_ms;
    if (elapsed_ms < 0) {
        return window_ms;
    }
    if (elapsed_ms >= window_ms) {
        return 0;
    }
    return window_ms - elapsed_ms;
}

static void ble_hid_gap_set_recovery_power_blocker(bool enabled, const char *reason)
{
    if (s_recovery_power_blocker_active == enabled) {
        return;
    }

    s_recovery_power_blocker_active = enabled;
    ESP_LOGI(TAG,
             "recovery: pairing power blocker %s reason=%s",
             enabled ? "held" : "released",
             reason != NULL ? reason : "unknown");
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
        enabled);
}

static void ble_hid_gap_recovery_pairing_window_timer_cb(void *arg)
{
    (void)arg;
    if (!s_recovery_pairing_window_active) {
        ble_hid_gap_set_recovery_power_blocker(false, "pairing_window_timer_inactive");
        return;
    }

    int64_t remaining_ms = ble_hid_gap_window_remaining_ms(
        s_recovery_pairing_window_opened_at_ms,
        BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS);
    if (remaining_ms <= 0) {
        ble_hid_gap_close_recovery_pairing_window("pairing_window_expired");
        return;
    }

    esp_err_t ret = esp_timer_start_once(
        s_recovery_pairing_window_timer,
        (uint64_t)remaining_ms * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "recovery: pairing window timer rearm failed ret=%s remaining_ms=%lld",
                 esp_err_to_name(ret),
                 (long long)remaining_ms);
    }
}

static void ble_hid_gap_arm_recovery_pairing_window_timer(void)
{
    if (s_recovery_pairing_window_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = ble_hid_gap_recovery_pairing_window_timer_cb,
            .name = "ble_recovery_pairing_window",
        };
        esp_err_t create_ret = esp_timer_create(&timer_args, &s_recovery_pairing_window_timer);
        if (create_ret != ESP_OK) {
            ESP_LOGW(TAG,
                     "recovery: pairing window timer create failed ret=%s",
                     esp_err_to_name(create_ret));
            return;
        }
    } else {
        (void)esp_timer_stop(s_recovery_pairing_window_timer);
    }

    esp_err_t start_ret = esp_timer_start_once(
        s_recovery_pairing_window_timer,
        (uint64_t)BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS * 1000ULL);
    if (start_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "recovery: pairing window timer start failed ret=%s",
                 esp_err_to_name(start_ret));
    }
}

static int64_t ble_hid_gap_recovery_pairing_window_remaining_ms(void)
{
    if (!s_recovery_pairing_window_active ||
        s_recovery_pairing_window_opened_at_ms <= 0) {
        return 0;
    }

    int64_t remaining_ms = ble_hid_gap_window_remaining_ms(
        s_recovery_pairing_window_opened_at_ms,
        BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS);
    if (remaining_ms <= 0) {
        ble_hid_gap_close_recovery_pairing_window("pairing_window_expired");
        return 0;
    }

    return remaining_ms;
}

static bool ble_hid_gap_recovery_pairing_window_open(void)
{
    return ble_hid_gap_recovery_pairing_window_remaining_ms() > 0;
}

static int64_t ble_hid_gap_recovery_swift_pair_prompt_remaining_ms(void)
{
    if (!ble_hid_gap_recovery_pairing_window_open() ||
        s_recovery_pairing_window_opened_at_ms <= 0) {
        return 0;
    }
    if (s_recovery_suppress_swift_pair_prompt) {
        if (!s_recovery_swift_pair_consumed) {
            ESP_LOGI(TAG, "recovery: Swift Pair prompt suppressed for Type-controlled silent recovery");
        }
        s_recovery_swift_pair_consumed = true;
        return 0;
    }
    if (s_recovery_swift_pair_consumed) {
        return 0;
    }
    if (BLE_HID_GAP_RECOVERY_SWIFT_PAIR_PROMPT_MS <= 0) {
        s_recovery_swift_pair_consumed = true;
        ESP_LOGI(TAG, "recovery: Swift Pair prompt disabled by build config; continuing normal pairable advertising");
        return 0;
    }

    int64_t remaining_ms = ble_hid_gap_window_remaining_ms(
        s_recovery_pairing_window_opened_at_ms,
        BLE_HID_GAP_RECOVERY_SWIFT_PAIR_PROMPT_MS);
    if (remaining_ms <= 0) {
        s_recovery_swift_pair_consumed = true;
        ESP_LOGI(TAG, "recovery: Swift Pair prompt window elapsed; continuing normal pairable advertising");
        return 0;
    }

    return remaining_ms;
}

static int64_t ble_hid_gap_first_pairing_window_remaining_ms(
    int bonded_peer_count)
{
    if (BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS <= 0) {
        s_first_pairing_window_opened_at_ms = 0;
        return 0;
    }

    if (bonded_peer_count > 0) {
        s_first_pairing_window_opened_at_ms = 0;
        return 0;
    }

    if (s_low_power_advertising || s_ble_gap_connected) {
        return 0;
    }

    if (s_first_pairing_window_opened_at_ms <= 0) {
        s_first_pairing_window_opened_at_ms = ble_hid_gap_now_ms();
        ESP_LOGI(TAG, "first-pairing Swift Pair window opened");
    }

    int64_t remaining_ms = ble_hid_gap_window_remaining_ms(
        s_first_pairing_window_opened_at_ms,
        BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS);
    if (remaining_ms <= 0) {
        ESP_LOGI(TAG, "first-pairing Swift Pair window expired");
    }
    return remaining_ms;
}

static int32_t ble_hid_gap_swift_pair_adv_duration_ms(int64_t remaining_ms)
{
    if (remaining_ms < BLE_HID_GAP_SWIFT_PAIR_ADV_MIN_RESTART_MS) {
        return (int32_t)BLE_HID_GAP_SWIFT_PAIR_ADV_MIN_RESTART_MS;
    }
    if (remaining_ms > 0x7fffffffLL) {
        return 0x7fffffff;
    }
    return (int32_t)remaining_ms;
}

static bool ble_hid_gap_recovery_pairing_needs_connectable_adv(void)
{
    return ble_hid_gap_recovery_pairing_window_open() && !s_ble_gap_connected;
}

static void ble_hid_gap_keep_recovery_adv_connectable(const char *reason)
{
    if (!ble_hid_gap_recovery_pairing_needs_connectable_adv()) {
        return;
    }

    const uint32_t state_flags =
        (s_low_power_advertising ? 2U : 0U) |
        (s_directed_adv_pending ? 4U : 0U) |
        (s_key_wake_only_advertising ? 8U : 0U);

    if (state_flags != 0U) {
        ESP_LOGI(TAG,
                 "recovery: keeping connectable advertising during pairing window: reason=%s flags=0x%lx",
                 reason != NULL ? reason : "unknown",
                 (unsigned long)state_flags);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 8, 0, state_flags, s_ble_gap_conn_handle);
    }

    s_low_power_advertising = false;
    s_key_wake_only_advertising = false;
    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;
}

static void ble_hid_gap_open_recovery_pairing_window(
    bool type_controlled,
    bool suppress_swift_pair_prompt)
{
    s_recovery_pairing_window_active = true;
    s_recovery_swift_pair_consumed = false;
    s_recovery_type_controlled_pairing = type_controlled;
    s_recovery_suppress_swift_pair_prompt = suppress_swift_pair_prompt;
    s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_recovery_pairing_window_opened_at_ms = ble_hid_gap_now_ms();
    ble_hid_gap_set_recovery_power_blocker(true, "pairing_window_open");
    ble_hid_gap_arm_recovery_pairing_window_timer();
    ESP_LOGI(TAG,
             "recovery: pairing window opened type_controlled=%u suppress_swift_pair=%u",
             type_controlled ? 1u : 0u,
             suppress_swift_pair_prompt ? 1u : 0u);
}

static void ble_hid_gap_close_recovery_pairing_window(const char *reason)
{
    if (!s_recovery_pairing_window_active) {
        return;
    }

    s_recovery_pairing_window_active = false;
    s_recovery_swift_pair_consumed = false;
    s_recovery_type_controlled_pairing = false;
    s_recovery_suppress_swift_pair_prompt = false;
    s_recovery_waiting_for_disconnect = false;
    s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_recovery_pairing_window_opened_at_ms = 0;
    if (s_recovery_pairing_window_timer != NULL) {
        (void)esp_timer_stop(s_recovery_pairing_window_timer);
    }
    ble_hid_gap_set_recovery_power_blocker(false, reason);
    ESP_LOGI(TAG, "recovery: pairing window closed: %s", reason);
    if (!s_ble_gap_connected &&
        reason != NULL &&
        strcmp(reason, "pairing_window_expired") == 0) {
        status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
    }
}

static bool ble_hid_gap_close_recovery_for_type_audio(const char *reason, uint16_t conn_handle)
{
    if (!ble_hid_gap_recovery_pairing_window_open()) {
        return false;
    }
    if (s_recovery_waiting_for_disconnect) {
        ESP_LOGI(TAG,
                 "recovery: ignoring Type audio ready on pre-reset connection reason=%s conn=%u; waiting for disconnect before pairing window can close",
                 reason != NULL ? reason : "type_audio_ready",
                 conn_handle);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 13, 1, 0, conn_handle);
        return false;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0 || (!desc.sec_state.encrypted && !desc.sec_state.bonded)) {
        ESP_LOGI(TAG,
                 "recovery: Type audio link observed before secure pairing; keeping pairing window open reason=%s conn=%u desc_rc=%d encrypted=%u bonded=%u",
                 reason != NULL ? reason : "type_audio_ready",
                 conn_handle,
                 rc,
                 rc == 0 ? desc.sec_state.encrypted : 0,
                 rc == 0 ? desc.sec_state.bonded : 0);
        return false;
    }

    ESP_LOGI(TAG,
             "recovery: Type audio link ready on secure connection; closing pairing window reason=%s conn=%u encrypted=%u bonded=%u",
             reason != NULL ? reason : "type_audio_ready",
             conn_handle,
             desc.sec_state.encrypted,
             desc.sec_state.bonded);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             11, 0, 0, conn_handle);
    ble_hid_gap_close_recovery_pairing_window(
        reason != NULL ? reason : "type_audio_ready");
    s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    return true;
}

bool ble_hid_gap_note_type_audio_ready(const char *reason)
{
    const char *ready_reason = reason != NULL ? reason : "type_audio_ready";
    if (ble_hid_gap_close_recovery_for_type_audio(ready_reason, s_ble_gap_conn_handle)) {
        return true;
    }

    if (s_ble_gap_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct ble_gap_conn_desc desc;
        int desc_rc = ble_gap_conn_find(s_ble_gap_conn_handle, &desc);
        const bool pairing_window_open = ble_hid_gap_recovery_pairing_window_open();
        if (desc_rc == 0 &&
            !s_recovery_waiting_for_disconnect &&
            (desc.sec_state.encrypted || desc.sec_state.bonded) &&
            (!pairing_window_open || desc.sec_state.bonded)) {
            ESP_LOGI(
                TAG,
                "type audio ready accepted on existing secure BLE connection reason=%s conn=%u encrypted=%u bonded=%u",
                ready_reason,
                s_ble_gap_conn_handle,
                desc.sec_state.encrypted,
                desc.sec_state.bonded);
            ble_hid_gap_note_secure_connection(
                s_ble_gap_conn_handle,
                "type audio ready existing secure connection");
            return true;
        }
        if (desc_rc != 0) {
            ESP_LOGW(
                TAG,
                "type audio ready connection descriptor lookup failed reason=%s conn=%u desc_rc=%d",
                ready_reason,
                s_ble_gap_conn_handle,
                desc_rc);
        }
    }

    int bonded_peer_count = 0;
    int rc = ble_hid_gap_get_bonded_peer_count(&bonded_peer_count);
    if (rc == 0 && bonded_peer_count == 0) {
        ESP_LOGW(
            TAG,
            "type audio ready rejected before BLE bond; keeping pairing window available without restarting repair reason=%s conn=%u",
            ready_reason,
            s_ble_gap_conn_handle);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 19, 0, 0, s_ble_gap_conn_handle);
        if (!ble_hid_gap_recovery_pairing_window_open()) {
            ble_hid_gap_open_recovery_pairing_window(true, true);
            if (!ble_gap_adv_active()) {
                esp_err_t adv_ret = ble_hid_gap_start_advertising();
                if (adv_ret != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "type audio ready unbonded advertising refresh failed ret=%s",
                        esp_err_to_name(adv_ret));
                    status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
                }
            }
        }
        return false;
    }

    ESP_LOGI(
        TAG,
        "type audio ready waiting for secure BLE state reason=%s conn=%u bonded_peers=%d lookup_rc=%d",
        ready_reason,
        s_ble_gap_conn_handle,
        bonded_peer_count,
        rc);
    return false;
}

static void ble_hid_gap_hold_recovery_pairing_led(const char *reason)
{
    int64_t remaining_ms = ble_hid_gap_recovery_pairing_window_remaining_ms();
    if (remaining_ms <= 0) {
        ESP_LOGI(TAG,
                 "recovery: pairing LED hold skipped because pairing window is closed: reason=%s",
                 reason != NULL ? reason : "unknown");
        return;
    }
    ESP_LOGI(TAG,
             "recovery: pairing LED hold continues without replaying repair cue: reason=%s remaining_ms=%lld",
             reason != NULL ? reason : "unknown",
             (long long)remaining_ms);
    status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
}

static void ble_hid_gap_note_secure_connection(uint16_t conn_handle, const char *reason)
{
    s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_hid_gap_set_secure_connection_state(true);
    if (s_recovery_type_controlled_pairing &&
        ble_hid_gap_recovery_pairing_window_open()) {
        ble_hid_gap_hold_recovery_pairing_led("type_controlled_secure_waiting_for_audio_notify");
        ESP_LOGI(TAG,
                 "recovery: secure BLE connection accepted, waiting for Type audio notify before closing pairing window: conn=%u reason=%s",
                 conn_handle,
                 reason != NULL ? reason : "unknown");
        return;
    }
    ble_hid_gap_close_recovery_pairing_window(reason != NULL ? reason : "secure connection established");
    status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, false);
    status_led_note_ble_boot_ready("secure_connection");
    ESP_LOGI(TAG,
             "secure BLE connection accepted for LED/link state: conn=%u reason=%s",
             conn_handle,
             reason != NULL ? reason : "unknown");
}

static int ble_hid_gap_get_bonded_peer_count(int *out_count)
{
    if (out_count == NULL) {
        return BLE_HS_EINVAL;
    }

    ble_addr_t bonded_peers[8];
    int bonded_peer_count = 0;
    int rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    if (rc == 0) {
        *out_count = bonded_peer_count;
    }
    return rc;
}

static void ble_hid_gap_request_recovery_security_once(uint16_t conn_handle, const char *reason)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        !ble_hid_gap_recovery_pairing_window_open()) {
        return;
    }
    if (s_recovery_security_request_conn_handle == conn_handle) {
        return;
    }
    if (s_recovery_security_failed_conn_handle == conn_handle) {
        ESP_LOGW(TAG, "recovery: security skipped reason=%s conn=%u after prior encryption failure; waiting for disconnect before retry",
                 reason != NULL ? reason : "unknown",
                 conn_handle);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 20, 0, 0, conn_handle);
        return;
    }

    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "recovery: security skipped reason=%s conn=%u desc_rc=%d",
                 reason != NULL ? reason : "unknown",
                 conn_handle,
                 rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 10, (uint32_t)rc, 0, conn_handle);
        return;
    }
    if (ble_hid_gap_recovery_bond_delete_active()) {
        ESP_LOGW(TAG,
                 "recovery: security skipped reason=%s conn=%u while async local bond delete is pending",
                 reason != NULL ? reason : "unknown",
                 conn_handle);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 17, 0, 0, conn_handle);
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return;
    }
    if (desc.sec_state.encrypted || desc.sec_state.bonded) {
        if (s_recovery_waiting_for_disconnect) {
            ESP_LOGI(TAG,
                     "recovery: ignoring secure state on pre-reset connection reason=%s conn=%u; waiting for disconnect before pairing window can close",
                     reason != NULL ? reason : "unknown",
                     conn_handle);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                     13, 0, 0, conn_handle);
            return;
        }
        ble_hid_gap_note_secure_connection(conn_handle, "secure connection already established");
        return;
    }

    s_recovery_security_request_conn_handle = conn_handle;
    ESP_LOGI(TAG,
             "recovery: waiting for Windows pairing security reason=%s conn=%u encrypted=%u bonded=%u",
             reason != NULL ? reason : "unknown",
             conn_handle,
             desc.sec_state.encrypted,
             desc.sec_state.bonded);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             10, 4, 0, conn_handle);
}

static uint16_t ble_hid_gap_get_service_changed_val_handle(void)
{
    if (s_service_changed_val_handle != BLE_HS_CONN_HANDLE_NONE) {
        return s_service_changed_val_handle;
    }

    const ble_uuid16_t gatt_uuid = BLE_UUID16_INIT(BLE_GATT_SVC_UUID16);
    const ble_uuid16_t service_changed_uuid =
        BLE_UUID16_INIT(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16);
    uint16_t def_handle = 0;
    uint16_t val_handle = 0;
    int rc = ble_gatts_find_chr(
        &gatt_uuid.u, &service_changed_uuid.u, &def_handle, &val_handle);
    if (rc != 0) {
        ESP_LOGW(TAG, "service changed characteristic lookup failed: rc=%d", rc);
        return BLE_HS_CONN_HANDLE_NONE;
    }

    s_service_changed_val_handle = val_handle;
    ESP_LOGI(TAG,
             "service changed characteristic handle: def=%u val=%u",
             def_handle,
             val_handle);
    return s_service_changed_val_handle;
}

static bool ble_hid_gap_stored_service_changed_schema_matches(const char *stored)
{
    if (strcmp(stored, s_service_changed_schema_id) == 0) {
        return true;
    }

    /*
     * Older firmware persisted "fw_version;schema".  Treat that as confirmed
     * when the schema suffix matches so normal OTA version changes do not force
     * Windows through another GATT cache refresh.
     */
    size_t stored_len = strlen(stored);
    size_t schema_len = strlen(s_service_changed_schema_id);
    if (stored_len <= schema_len + 1U) {
        return false;
    }
    size_t suffix_index = stored_len - schema_len;
    return stored[suffix_index - 1U] == ';' &&
           strcmp(&stored[suffix_index], s_service_changed_schema_id) == 0;
}

static bool ble_hid_gap_service_changed_pending(void)
{
    if (s_service_changed_state_loaded) {
        return s_service_changed_pending;
    }

    snprintf(
        s_service_changed_schema_id,
        sizeof(s_service_changed_schema_id),
        "%s",
        BLE_HID_GAP_GATT_SCHEMA_REV);

    char stored_schema[sizeof(s_service_changed_schema_id)] = {0};
    size_t stored_len = sizeof(stored_schema);
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READONLY,
        &nvs);
    if (ret == ESP_OK) {
        ret = nvs_get_str(
            nvs,
            BLE_HID_GAP_SERVICE_CHANGED_STATE_KEY,
            stored_schema,
            &stored_len);
        nvs_close(nvs);
    }

    if (ret == ESP_OK) {
        s_service_changed_pending =
            !ble_hid_gap_stored_service_changed_schema_matches(stored_schema);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_service_changed_pending = true;
    } else {
        s_service_changed_pending = true;
        ESP_LOGW(TAG, "service changed schema state unavailable: %s", esp_err_to_name(ret));
    }

    s_service_changed_state_loaded = true;
    ESP_LOGI(
        TAG,
        "service changed schema state: current=%s stored=%s pending=%u",
        s_service_changed_schema_id,
        ret == ESP_OK ? stored_schema : "none",
        s_service_changed_pending ? 1U : 0U);
    return s_service_changed_pending;
}

static void ble_hid_gap_mark_service_changed_confirmed(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs);
    if (ret == ESP_OK) {
        ret = nvs_set_str(
            nvs,
            BLE_HID_GAP_SERVICE_CHANGED_STATE_KEY,
            s_service_changed_schema_id);
        if (ret == ESP_OK) {
            ret = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }

    if (ret == ESP_OK) {
        s_service_changed_pending = false;
        ESP_LOGI(
            TAG,
            "service changed confirmed for schema_id=%s; future reconnects skip GATT refresh",
            s_service_changed_schema_id);
    } else {
        ESP_LOGW(TAG, "service changed confirmation persist failed: %s", esp_err_to_name(ret));
    }
}

static void ble_hid_gap_queue_service_changed(const char *reason)
{
    if (!ble_hid_gap_service_changed_pending()) {
        ESP_LOGI(TAG, "service changed skipped for %s: schema already confirmed", reason);
        return;
    }
    if (s_service_changed_queued_for_conn) {
        ESP_LOGI(TAG, "service changed already queued for this connection: reason=%s", reason);
        return;
    }

    uint16_t service_changed_val_handle = ble_hid_gap_get_service_changed_val_handle();
    if (service_changed_val_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "service changed not queued for %s: characteristic unavailable", reason);
        return;
    }

    ble_svc_gatt_changed(
        BLE_HID_GAP_SERVICE_CHANGED_START_HANDLE,
        BLE_HID_GAP_SERVICE_CHANGED_END_HANDLE);
    ESP_LOGI(TAG,
             "service changed marked for %s: attr_handle=%u schema_id=%s range=0x0001-0xffff",
             reason,
             service_changed_val_handle,
             s_service_changed_schema_id);
}

static void ble_hid_gap_indicate_service_changed(uint16_t conn_handle, const char *reason)
{
    if (!ble_hid_gap_service_changed_pending()) {
        ESP_LOGI(TAG, "service changed indication skipped for %s: schema already confirmed", reason);
        return;
    }
    if (s_service_changed_queued_for_conn) {
        ESP_LOGI(TAG, "service changed indication skipped for %s: update already queued", reason);
        return;
    }

    uint16_t service_changed_val_handle = ble_hid_gap_get_service_changed_val_handle();
    if (service_changed_val_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "service changed indication not sent for %s: characteristic unavailable", reason);
        return;
    }

    const uint8_t payload[] = {0x01, 0x00, 0xff, 0xff};
    struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, sizeof(payload));
    if (om == NULL) {
        ESP_LOGW(TAG, "service changed indication not sent for %s: no mbuf", reason);
        return;
    }

    int rc = ble_gatts_indicate_custom(conn_handle, service_changed_val_handle, om);
    if (rc == 0) {
        s_service_changed_queued_for_conn = true;
        ESP_LOGI(TAG,
                 "service changed indication sent for %s: conn_handle=%u attr_handle=%u schema_id=%s range=0x0001-0xffff",
                 reason,
                 conn_handle,
                 service_changed_val_handle,
                 s_service_changed_schema_id);
    } else {
        ESP_LOGW(TAG,
                 "service changed indication send deferred for %s: conn_handle=%u attr_handle=%u rc=%d",
                 reason,
                 conn_handle,
                 service_changed_val_handle,
                 rc);
    }
}

static esp_err_t ble_hid_gap_request_connection_params(
    const char *policy,
    uint16_t itvl_min,
    uint16_t itvl_max,
    uint16_t latency,
    uint16_t supervision_timeout,
    ble_hid_conn_param_mode_t mode)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot(policy != NULL ? policy : "connection_params");
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t last_mode = ble_hid_gap_last_conn_param_mode();
    const bool actual_params_match = ble_hid_gap_conn_desc_matches_params(
        conn.conn_handle,
        itvl_min,
        itvl_max,
        latency);
    if (actual_params_match) {
        portENTER_CRITICAL(&s_ble_gap_state_lock);
        if (s_ble_gap_connected && s_ble_gap_conn_handle == conn.conn_handle) {
            s_last_conn_param_mode = (uint32_t)mode;
            s_conn_param_request_pending_until_tick = 0;
        }
        portEXIT_CRITICAL(&s_ble_gap_state_lock);
        ESP_LOGI(
            TAG,
            "%s connection parameters already active: conn=%u preferred_itvl=%u-%u latency=%u timeout=%u mode=%u",
            policy,
            conn.conn_handle,
            itvl_min,
            itvl_max,
            latency,
            supervision_timeout,
            (unsigned)mode);
        return ESP_OK;
    }
    if (ble_hid_gap_conn_param_retry_is_deferred()) {
        ESP_LOGI(
            TAG,
            "%s connection parameter retry deferred after central transaction collision: conn=%u mode=%u",
            policy,
            conn.conn_handle,
            (unsigned)mode);
        return ESP_OK;
    }
    if (last_mode != 0 && ble_hid_gap_conn_param_request_is_pending()) {
        ESP_LOGW(
            TAG,
            "%s connection parameter update pending confirmation: conn=%u requested_mode=%u desired_mode=%u",
            policy,
            conn.conn_handle,
            (unsigned)last_mode,
            (unsigned)mode);
        return ESP_OK;
    }
    if (last_mode != 0) {
        ble_hid_gap_clear_conn_param_mode("connection parameters superseded by link state");
    }

    struct ble_gap_upd_params params = {
        .itvl_min = itvl_min,
        .itvl_max = itvl_max,
        .latency = latency,
        .supervision_timeout = supervision_timeout,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(conn.conn_handle, &params);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        portENTER_CRITICAL(&s_ble_gap_state_lock);
        if (s_ble_gap_connected && s_ble_gap_conn_handle == conn.conn_handle) {
            s_last_conn_param_mode = (uint32_t)mode;
            s_conn_param_retry_not_before_tick = 0;
            /* NimBLE owns the actual transaction lifetime.  BLE_HS_EALREADY
             * means its connection-update entry remains active, so wait for
             * BLE_GAP_EVENT_CONN_UPDATE instead of racing a local timeout. */
            s_conn_param_request_pending_until_tick = portMAX_DELAY;
        }
        portEXIT_CRITICAL(&s_ble_gap_state_lock);
    }

    ESP_LOGI(
        TAG,
        "%s connection parameter update requested: conn=%u preferred_itvl=%u-%u latency=%u timeout=%u mode=%u rc=%d",
        policy,
        conn.conn_handle,
        itvl_min,
        itvl_max,
        latency,
        supervision_timeout,
        (unsigned)mode,
        rc);
    diag_log(
        DIAG_SRC_BLE_GAP,
        DIAG_GAP_CONN_PARAM_REQ,
        rc == 0 || rc == BLE_HS_EALREADY ? DIAG_SEV_INFO : DIAG_SEV_WARN,
        (uint32_t)mode,
        (uint32_t)rc,
        conn.conn_handle,
        latency);
    ble_hid_gap_log_conn_desc(policy, conn.conn_handle);
    return (rc == 0 || rc == BLE_HS_EALREADY) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ble_hid_gap_request_preferred_2m_phy(const char *policy)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot(policy != NULL ? policy : "preferred_2m_phy");
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t tx_phys_mask = BLE_GAP_LE_PHY_2M_MASK;
    const uint8_t rx_phys_mask = BLE_GAP_LE_PHY_2M_MASK;
    int rc = ble_gap_set_prefered_le_phy(conn.conn_handle, tx_phys_mask, rx_phys_mask, 0);
    const bool accepted = rc == 0 || rc == BLE_HS_EALREADY;
    if (accepted) {
        ESP_LOGI(
            TAG,
            "%s 2M PHY update requested: conn=%u tx_mask=0x%02x rx_mask=0x%02x rc=%d",
            policy,
            conn.conn_handle,
            tx_phys_mask,
            rx_phys_mask,
            rc);
    } else {
        ESP_LOGW(
            TAG,
            "%s 2M PHY update request failed: conn=%u tx_mask=0x%02x rx_mask=0x%02x rc=%d",
            policy,
            conn.conn_handle,
            tx_phys_mask,
            rx_phys_mask,
            rc);
    }
    diag_log(
        DIAG_SRC_BLE_GAP,
        DIAG_GAP_PHY,
        accepted ? DIAG_SEV_INFO : DIAG_SEV_WARN,
        (uint32_t)rc,
        tx_phys_mask,
        rx_phys_mask,
        conn.conn_handle);
    return accepted ? ESP_OK : ESP_FAIL;
}

static bool ble_hid_gap_configure_swift_pair_fields(void)
{
    if (s_adv_device_name == NULL || s_adv_device_name_len == 0) {
        return false;
    }

    const bool include_appearance =
        s_adv_device_name_len <= BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_APPEARANCE;
    if (s_adv_device_name_len >
        BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITHOUT_APPEARANCE) {
        return false;
    }

    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    memset(&s_scan_rsp_fields, 0, sizeof(s_scan_rsp_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    if (include_appearance) {
        s_adv_fields.appearance = s_adv_appearance;
        s_adv_fields.appearance_is_present = 1;
    }
    if (s_adv_device_name_len <=
        BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_HID_UUID) {
        s_adv_fields.uuids16 = &s_hid_service_uuid;
        s_adv_fields.num_uuids16 = 1;
        s_adv_fields.uuids16_is_complete = 1;
    }

    memcpy(
        s_swift_pair_mfg_payload,
        s_swift_pair_mfg_data,
        BLE_HID_SWIFT_PAIR_MFG_DATA_LEN);
    memcpy(
        s_swift_pair_mfg_payload + BLE_HID_SWIFT_PAIR_MFG_DATA_LEN,
        s_adv_device_name,
        s_adv_device_name_len);
    s_adv_fields.mfg_data = s_swift_pair_mfg_payload;
    s_adv_fields.mfg_data_len =
        BLE_HID_SWIFT_PAIR_MFG_DATA_LEN + s_adv_device_name_len;

    s_scan_rsp_fields.name = (uint8_t *)s_adv_device_name;
    s_scan_rsp_fields.name_len = s_adv_device_name_len;
    s_scan_rsp_fields.name_is_complete = 1;
    return true;
}

static bool ble_hid_gap_configure_normal_adv_fields(void)
{
    if (s_adv_device_name == NULL || s_adv_device_name_len == 0) {
        return false;
    }

    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    memset(&s_scan_rsp_fields, 0, sizeof(s_scan_rsp_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = s_adv_appearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.uuids16 = &s_hid_service_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    bool name_in_adv = s_adv_device_name_len <= BLE_HID_ADV_NAME_MAX_LEN;
    if (name_in_adv) {
        s_adv_fields.name = (uint8_t *)s_adv_device_name;
        s_adv_fields.name_len = s_adv_device_name_len;
        s_adv_fields.name_is_complete = 1;

        s_scan_rsp_fields.tx_pwr_lvl_is_present = 1;
        s_scan_rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
        s_scan_rsp_fields.uuids128 = &s_audio_stream_service_uuid;
        s_scan_rsp_fields.num_uuids128 = 1;
        /* The OTA GATT service is another 128-bit service; the legacy 31-byte scan
         * response can only afford one UUID, so this advertised list is incomplete.
         */
        s_scan_rsp_fields.uuids128_is_complete = 0;
    } else {
        s_scan_rsp_fields.name = (uint8_t *)s_adv_device_name;
        s_scan_rsp_fields.name_len = s_adv_device_name_len;
        s_scan_rsp_fields.name_is_complete = 1;
    }
    return name_in_adv;
}

static bool ble_hid_gap_configure_type_controlled_recovery_adv_fields(void)
{
    if (s_adv_device_name == NULL || s_adv_device_name_len == 0) {
        return false;
    }

    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    memset(&s_scan_rsp_fields, 0, sizeof(s_scan_rsp_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = s_adv_appearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.uuids16 = &s_hid_service_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    bool name_in_adv = s_adv_device_name_len <= BLE_HID_ADV_NAME_MAX_LEN;
    if (name_in_adv) {
        s_adv_fields.name = (uint8_t *)s_adv_device_name;
        s_adv_fields.name_len = s_adv_device_name_len;
        s_adv_fields.name_is_complete = 1;

        s_scan_rsp_fields.tx_pwr_lvl_is_present = 1;
        s_scan_rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
        s_scan_rsp_fields.uuids128 = &s_audio_stream_service_uuid;
        s_scan_rsp_fields.num_uuids128 = 1;
        s_scan_rsp_fields.uuids128_is_complete = 0;
    } else {
        s_scan_rsp_fields.name = (uint8_t *)s_adv_device_name;
        s_scan_rsp_fields.name_len = s_adv_device_name_len;
        s_scan_rsp_fields.name_is_complete = 1;
    }

    return true;
}

static esp_err_t ble_hid_gap_refresh_configured_device_name(const char *context)
{
    const char *device_name = listener_device_get_ble_name();
    if (device_name == NULL || device_name[0] == '\0') {
        ESP_LOGW(TAG, "%s: configured BLE name is empty", context);
        return ESP_ERR_INVALID_STATE;
    }

    const size_t device_name_len = strlen(device_name);
    bool changed = s_adv_device_name == NULL ||
                   s_adv_device_name_len != device_name_len ||
                   strncmp(s_adv_device_name, device_name, device_name_len) != 0;
    s_adv_device_name = device_name;
    s_adv_device_name_len = device_name_len;

    int gap_name_rc = ble_svc_gap_device_name_set(device_name);
    if (gap_name_rc != 0) {
        ESP_LOGW(TAG, "%s: ble_svc_gap_device_name_set failed rc=%d name=%s",
                 context,
                 gap_name_rc,
                 device_name);
        return ESP_FAIL;
    }
    int gap_appearance_rc = ble_svc_gap_device_appearance_set(s_adv_appearance);
    if (gap_appearance_rc != 0) {
        ESP_LOGW(TAG, "%s: ble_svc_gap_device_appearance_set failed rc=%d appearance=0x%04x",
                 context,
                 gap_appearance_rc,
                 s_adv_appearance);
        return ESP_FAIL;
    }

    bool name_in_adv = ble_hid_gap_configure_normal_adv_fields();
    device_settings_mark_ble_name_applied();
    ESP_LOGI(TAG,
             "%s: BLE identity refreshed name=%s len=%u changed=%u appearance=0x%04x name_in_adv=%s",
             context,
             s_adv_device_name,
             (unsigned)s_adv_device_name_len,
             changed ? 1u : 0u,
             s_adv_appearance,
             name_in_adv ? "yes" : "scan_rsp");
    return ESP_OK;
}

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    s_adv_device_name = device_name;
    s_adv_device_name_len = strlen(device_name);
    s_adv_appearance = appearance;
    int gap_appearance_rc = ble_svc_gap_device_appearance_set(s_adv_appearance);
    if (gap_appearance_rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_appearance_set failed rc=%d appearance=0x%04x",
                 gap_appearance_rc,
                 s_adv_appearance);
        return ESP_FAIL;
    }
    bool name_in_adv = ble_hid_gap_configure_normal_adv_fields();

    /* Initialize the security configuration */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    /*
     * This no-IO HID path uses unauthenticated Just Works. Keep the SMP path
     * legacy-compatible; with no display/input, Windows PairAsync can otherwise
     * time out during LE Secure Connections feature exchange.
     */
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    ESP_LOGI(
        TAG,
        "NimBLE advertising configured: appearance=0x%04x name=%s name_in_adv=%s io_cap=%u bonding=%u mitm=%u sc=%u",
        s_adv_appearance,
        s_adv_device_name,
        name_in_adv ? "yes" : "scan_rsp",
        ble_hs_cfg.sm_io_cap,
        ble_hs_cfg.sm_bonding,
        ble_hs_cfg.sm_mitm,
        ble_hs_cfg.sm_sc);

    return ESP_OK;
}

static void ble_hid_gap_handle_connect_established(uint16_t conn_handle, const char *source)
{
    struct ble_gap_conn_desc desc;
    int rc;

    if (ble_hid_gap_recovery_bond_delete_active()) {
        ESP_LOGW(TAG,
                 "recovery: rejecting connection while async local bond delete is pending conn=%u source=%s",
                 conn_handle,
                 source != NULL ? source : "unknown");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 17, 0, 0, conn_handle);
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return;
    }

    ble_hid_gap_connection_snapshot_t snapshot = ble_hid_gap_connection_snapshot();
    const bool duplicate_connect = snapshot.connected && snapshot.conn_handle == conn_handle;
    if (duplicate_connect) {
        ESP_LOGI(TAG,
                 "connection already tracked; source=%s conn=%u secure=%u",
                 source != NULL ? source : "unknown",
                 conn_handle,
                 snapshot.secure_connected ? 1U : 0U);
    } else {
        if (snapshot.connected && snapshot.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGW(TAG,
                     "connection replaced stale GAP state; source=%s old_conn=%u new_conn=%u old_secure=%u",
                     source != NULL ? source : "unknown",
                     snapshot.conn_handle,
                     conn_handle,
                     snapshot.secure_connected ? 1U : 0U);
        }
        ble_hid_gap_set_connection_state(true, conn_handle);
        ble_diag_log_on_gap_connect(conn_handle);
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_connect(conn_handle);
        }
        s_last_adv_was_directed = false;
        s_service_changed_queued_for_conn = false;
        ble_hid_gap_queue_service_changed("connect");
    }

    rc = ble_gap_conn_find(conn_handle, &desc);
    const bool conn_desc_valid = rc == 0;
    if (rc == 0) {
        ESP_LOGI(
            TAG,
            "security state before initiate: source=%s encrypted=%u authenticated=%u bonded=%u key_size=%u",
            source != NULL ? source : "unknown",
            desc.sec_state.encrypted,
            desc.sec_state.authenticated,
            desc.sec_state.bonded,
            desc.sec_state.key_size);
        if (!duplicate_connect) {
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CONN_PARAM, DIAG_SEV_INFO,
                     desc.conn_itvl, desc.conn_latency, desc.supervision_timeout, conn_handle);
        }
    } else {
        ESP_LOGW(TAG,
                 "connection descriptor lookup failed before security initiate: source=%s conn=%u rc=%d",
                 source != NULL ? source : "unknown",
                 conn_handle,
                 rc);
    }

    const bool recovery_pairing_window = ble_hid_gap_recovery_pairing_window_open();
    if (conn_desc_valid && recovery_pairing_window) {
        ble_hid_gap_hold_recovery_pairing_led("ble_recovery_windows_connecting");
        ESP_LOGI(
            TAG,
            "recovery: Windows connection during pairing window; Listener waiting for Windows pairing security while keeping pairing window visible until secure pairing conn=%u source=%s",
            conn_handle,
            source != NULL ? source : "unknown");
        if (!duplicate_connect) {
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                     12, 0, 0, conn_handle);
        }
        ble_hid_gap_request_recovery_security_once(conn_handle, source != NULL ? source : "connect");
    } else if (conn_desc_valid && desc.sec_state.encrypted) {
        ble_hid_gap_note_secure_connection(conn_handle, "connect already encrypted");
    } else if (conn_desc_valid) {
        rc = ble_gap_security_initiate(conn_handle);
        if (rc == 0) {
            ESP_LOGI(TAG,
                     "security initiate requested conn=%u source=%s",
                     conn_handle,
                     source != NULL ? source : "unknown");
        } else if (rc == BLE_HS_EALREADY) {
            ESP_LOGI(TAG,
                     "security already in progress conn=%u source=%s",
                     conn_handle,
                     source != NULL ? source : "unknown");
        } else {
            ESP_LOGW(TAG,
                     "security initiate failed conn=%u rc=%d source=%s",
                     conn_handle,
                     rc,
                     source != NULL ? source : "unknown");
        }
    } else {
        ESP_LOGW(TAG,
                 "security initiate skipped: missing connection descriptor conn=%u source=%s",
                 conn_handle,
                 source != NULL ? source : "unknown");
    }

    if (s_audio_enabled && !duplicate_connect) {
        ESP_LOGI(TAG, "audio high-speed link request deferred until active recording");
    }
}

static void ble_hid_gap_handle_disconnect(uint16_t conn_handle, int reason, const char *source)
{
    int rc;
    const int64_t now_ms = ble_hid_gap_now_ms();
    const bool duplicate_disconnect =
        s_last_disconnect_event_conn_handle == conn_handle &&
        s_last_disconnect_event_reason == reason &&
        now_ms - s_last_disconnect_event_at_ms >= 0 &&
        now_ms - s_last_disconnect_event_at_ms < 750;
    if (duplicate_disconnect) {
        ble_hid_gap_connection_snapshot_t active_snapshot = ble_hid_gap_connection_snapshot();
        if (!active_snapshot.connected || active_snapshot.conn_handle != conn_handle) {
            ESP_LOGI(TAG,
                     "duplicate disconnect event ignored; source=%s conn=%u reason=%d",
                     source != NULL ? source : "unknown",
                     conn_handle,
                     reason);
            return;
        }
        ESP_LOGW(TAG,
                 "duplicate disconnect event matches active tracked connection; processing to clear state source=%s conn=%u reason=%d secure=%u",
                 source != NULL ? source : "unknown",
                 conn_handle,
                 reason,
                 active_snapshot.secure_connected ? 1U : 0U);
    }
    s_last_disconnect_event_conn_handle = conn_handle;
    s_last_disconnect_event_reason = reason;
    s_last_disconnect_event_at_ms = now_ms;

    ESP_LOGI(TAG, "disconnect; source=%s reason=%d", source != NULL ? source : "unknown", reason);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_WARN,
             0, (uint32_t)reason, conn_handle, 0);
    ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
    if (s_recovery_security_request_conn_handle == conn_handle) {
        s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_recovery_security_failed_conn_handle == conn_handle) {
        s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
    s_service_changed_queued_for_conn = false;
    if (s_audio_enabled) {
        ble_audio_stream_on_gap_disconnect(conn_handle);
    }
    ble_diag_log_on_gap_disconnect(conn_handle);
    ble_firmware_ota_on_gap_disconnect(conn_handle);
    s_directed_adv_pending = true;
    s_last_adv_was_directed = false;
    if (s_recovery_waiting_for_disconnect) {
        ESP_LOGI(TAG,
                 "recovery: pre-reset connection disconnected; pairing window remains open for the new bond");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 14, 0, 0, conn_handle);
        s_recovery_waiting_for_disconnect = false;
    }
    if (s_shutdown_quiesce) {
        s_directed_adv_pending = false;
        ESP_LOGW(TAG, "shutdown quiesce active: suppressing advertising restart after disconnect");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_DISCONNECT_SUPPRESS,
            (uint32_t)reason,
            ble_hid_gap_adv_active_snapshot(),
            conn_handle,
            DIAG_SEV_WARN);
        return;
    }
    ble_hid_gap_keep_recovery_adv_connectable("disconnect");
    if (s_key_wake_only_advertising) {
        s_directed_adv_pending = false;
        ESP_LOGI(TAG, "key-wake-only idle: suppressing advertising restart after disconnect");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_DISCONNECT_SUPPRESS,
            (uint32_t)reason,
            ble_hid_gap_adv_active_snapshot(),
            conn_handle,
            DIAG_SEV_INFO);
        return;
    }
    if (ble_hid_gap_recovery_bond_delete_active()) {
        ESP_LOGW(TAG, "recovery: advertising deferred after disconnect until async local bond delete completes");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 16, 0, 0, conn_handle);
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return;
    }
    bool disconnect_recovery_pairing_window = ble_hid_gap_recovery_pairing_window_open();
    if (disconnect_recovery_pairing_window) {
        int bonded_peer_count = 0;
        rc = ble_hid_gap_get_bonded_peer_count(&bonded_peer_count);
        if (rc == 0 && bonded_peer_count > 0) {
            ESP_LOGI(TAG, "recovery: bonded peer present after disconnect; keeping pairing window visible until secure reconnect");
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                     9, (uint32_t)bonded_peer_count, 0, conn_handle);
        } else if (rc != 0) {
            ESP_LOGW(TAG, "recovery: bonded peer lookup after disconnect failed rc=%d; keeping pairing window", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                      9, (uint32_t)rc, 0, conn_handle);
        }
    }
    if (disconnect_recovery_pairing_window && s_native_recovery_identity_rotate_pending) {
        esp_err_t identity_ret =
            ble_hid_gap_rotate_native_recovery_identity("recovery_disconnect");
        if (identity_ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "recovery: native identity rotation after disconnect failed: %s",
                esp_err_to_name(identity_ret));
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
            return;
        }
    }
    esp_err_t adv_ret = ble_hid_gap_start_advertising();
    if (disconnect_recovery_pairing_window) {
        if (adv_ret != ESP_OK) {
            ESP_LOGE(TAG, "recovery: advertising restart failed after disconnect: %s",
                     esp_err_to_name(adv_ret));
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_adv_restart_failed");
            return;
        }
        ESP_LOGW(
            TAG,
            "recovery: pairing reset continues after disconnect, BLE identity ready and device is discoverable for re-pair native_rotated=%u",
            s_native_recovery_random_identity_active ? 1u : 0u);
        status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE);
        ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_window_after_disconnect");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 4, 0, 0, s_ble_gap_conn_handle);
    }
}

static int ble_hid_gap_global_event_listener(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG,
                     "global GAP listener connection established; conn=%u",
                     event->connect.conn_handle);
            ble_hid_gap_handle_connect_established(
                event->connect.conn_handle,
                "global_gap_connect");
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_hid_gap_handle_disconnect(
            event->disconnect.conn.conn_handle,
            event->disconnect.reason,
            "global_gap_disconnect");
        return 0;
    default:
        return 0;
    }
}

static void ble_hid_gap_register_global_event_listener_once(const char *reason)
{
    if (s_ble_hid_gap_event_listener_registered) {
        return;
    }

    int rc = ble_gap_event_listener_register(
        &s_ble_hid_gap_event_listener,
        ble_hid_gap_global_event_listener,
        NULL);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_ble_hid_gap_event_listener_registered = true;
        ESP_LOGI(TAG,
                 "global GAP event listener registered: reason=%s rc=%d",
                 reason != NULL ? reason : "unknown",
                 rc);
    } else {
        ESP_LOGW(TAG,
                 "global GAP event listener registration failed: reason=%s rc=%d",
                 reason != NULL ? reason : "unknown",
                 rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_WARN,
                 7, (uint32_t)rc, 0, 0);
    }
}

static int
nimble_hid_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status != 0) {
            ble_hid_gap_connection_snapshot_t stale_conn = ble_hid_gap_connection_snapshot();
            if (stale_conn.connected && stale_conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                ESP_LOGW(
                    TAG,
                    "connection failed while previous GAP link was still marked connected; clearing stale link status=%d failed_conn=%u stale_conn=%u secure=%u",
                    event->connect.status,
                    event->connect.conn_handle,
                    stale_conn.conn_handle,
                    stale_conn.secure_connected ? 1U : 0U);
                ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
                s_service_changed_queued_for_conn = false;
                if (s_audio_enabled) {
                    ble_audio_stream_on_gap_disconnect(stale_conn.conn_handle);
                }
                ble_diag_log_on_gap_disconnect(stale_conn.conn_handle);
                ble_firmware_ota_on_gap_disconnect(stale_conn.conn_handle);
                status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
            }
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_WARN,
                     0, (uint32_t)event->connect.status, event->connect.conn_handle, 0);
            s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            /*
             * Connection failures are expected while Windows retries a stale
             * cache entry, while directed advertising falls back, and during
             * the user-requested re-pairing window. Keep the visible state in
             * BLE pairing/reconnecting; reserve WARN for explicit recovery,
             * advertising, HID-send, or stack failures that require action.
             */
            if (s_shutdown_quiesce) {
                ESP_LOGW(TAG, "shutdown quiesce active: suppressing advertising after connect failure");
                ble_hid_gap_log_adv_state(
                    BLE_HID_GAP_ADV_STATE_CONNECT_FAIL_SUPPRESS,
                    (uint32_t)event->connect.status,
                    ble_hid_gap_adv_active_snapshot(),
                    event->connect.conn_handle,
                    DIAG_SEV_WARN);
                return 0;
            }
            ble_hid_gap_keep_recovery_adv_connectable("connect failure");
            if (s_key_wake_only_advertising) {
                ESP_LOGI(TAG, "key-wake-only idle: suppressing advertising after connect failure");
                ble_hid_gap_log_adv_state(
                    BLE_HID_GAP_ADV_STATE_CONNECT_FAIL_SUPPRESS,
                    (uint32_t)event->connect.status,
                    ble_hid_gap_adv_active_snapshot(),
                    event->connect.conn_handle,
                    DIAG_SEV_INFO);
                return 0;
            }
            s_directed_adv_pending = false;
            ble_hid_gap_start_advertising();
            return 0;
        }

        ble_hid_gap_handle_connect_established(event->connect.conn_handle, "adv_gap_connect");
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ble_hid_gap_handle_disconnect(
            event->disconnect.conn.conn_handle,
            event->disconnect.reason,
            "adv_gap_disconnect");
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d",
                event->conn_update.status);
        if (event->conn_update.status == BLE_HS_HCI_ERR(BLE_ERR_DIFF_TRANS_COLL)) {
            ble_hid_gap_defer_conn_param_retry_after_collision(event->conn_update.conn_handle);
        } else if (event->conn_update.status != 0) {
            ble_hid_gap_clear_conn_param_mode("connection update failed");
        } else {
            ble_hid_gap_confirm_conn_param_update(event->conn_update.conn_handle);
        }
        ble_hid_gap_log_conn_desc("connection updated", event->conn_update.conn_handle);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc == 0) {
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CONN_PARAM, DIAG_SEV_INFO,
                     desc.conn_itvl, desc.conn_latency, desc.supervision_timeout, event->conn_update.conn_handle);
        }
        if (ble_hid_gap_active_connection_required()) {
            if (ble_hid_gap_active_connection_applied()) {
                ESP_LOGI(TAG,
                         "active connection promotion confirmed: conn=%u target_itvl=%u-%u latency=%u",
                         event->conn_update.conn_handle,
                         BLE_HID_GAP_ACTIVE_ITVL_MIN,
                         BLE_HID_GAP_ACTIVE_ITVL_MAX,
                         BLE_HID_GAP_ACTIVE_LATENCY);
                (void)ble_hid_gap_request_preferred_2m_phy("active audio");
            } else {
                (void)ble_hid_gap_schedule_active_connection_with_delay(
                    0U,
                    "connection update completed below active parameters");
            }
        }
        return 0;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(
            TAG,
            "PHY update complete; status=%d conn_handle=%u tx_phy=%u rx_phy=%u",
            event->phy_updated.status,
            event->phy_updated.conn_handle,
            event->phy_updated.tx_phy,
            event->phy_updated.rx_phy);
        diag_log(DIAG_SRC_BLE_GAP,
                 DIAG_GAP_PHY,
                 event->phy_updated.status == 0 ? DIAG_SEV_INFO : DIAG_SEV_WARN,
                 (uint32_t)event->phy_updated.status,
                 event->phy_updated.tx_phy,
                 event->phy_updated.rx_phy,
                 event->phy_updated.conn_handle);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d",
                event->adv_complete.reason);
        if (s_last_adv_was_directed) {
            ESP_LOGI(TAG, "directed advertising completed; falling back to undirected advertising");
            s_last_adv_was_directed = false;
        }
        if (s_shutdown_quiesce) {
            ESP_LOGW(TAG, "shutdown quiesce active: suppressing advertising restart after adv complete");
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_ADV_COMPLETE_SUPPRESS,
                (uint32_t)event->adv_complete.reason,
                ble_hid_gap_adv_active_snapshot(),
                s_ble_gap_conn_handle,
                DIAG_SEV_WARN);
            return 0;
        }
        ble_hid_gap_keep_recovery_adv_connectable("adv complete");
        if (s_key_wake_only_advertising) {
            ESP_LOGI(TAG, "key-wake-only idle: suppressing advertising restart after adv complete");
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_ADV_COMPLETE_SUPPRESS,
                (uint32_t)event->adv_complete.reason,
                ble_hid_gap_adv_active_snapshot(),
                s_ble_gap_conn_handle,
                DIAG_SEV_INFO);
            return 0;
        }
        ble_hid_gap_start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                event->subscribe.conn_handle,
                event->subscribe.attr_handle,
                event->subscribe.reason,
                event->subscribe.prev_notify,
                event->subscribe.cur_notify,
                event->subscribe.prev_indicate,
                event->subscribe.cur_indicate);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_SUBSCRIBE, DIAG_SEV_INFO,
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 ((uint32_t)event->subscribe.reason << 16) |
                 ((uint32_t)event->subscribe.prev_notify << 8) |
                 (uint32_t)event->subscribe.cur_notify,
                 ((uint32_t)event->subscribe.prev_indicate << 8) |
                 (uint32_t)event->subscribe.cur_indicate);
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_subscribe(
                event->subscribe.conn_handle,
                event->subscribe.attr_handle,
                event->subscribe.cur_notify,
                event->subscribe.cur_indicate);
        }
        if (event->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_WRITE &&
            event->subscribe.attr_handle == ble_hid_gap_get_service_changed_val_handle() &&
            event->subscribe.cur_indicate != 0) {
            ble_hid_gap_indicate_service_changed(event->subscribe.conn_handle, "central subscribe");
        }
        if (s_audio_enabled &&
            event->subscribe.attr_handle == ble_audio_stream_get_notify_attr_handle() &&
            event->subscribe.cur_notify != 0) {
            ble_hid_gap_log_conn_desc("audio notify subscribed", event->subscribe.conn_handle);
            if (ble_hid_gap_close_recovery_for_type_audio(
                    "audio_notify_subscribe",
                    event->subscribe.conn_handle)) {
                return 0;
            }
        }
        ble_hid_gap_request_recovery_security_once(event->subscribe.conn_handle, "subscribe");
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                event->mtu.conn_handle,
                event->mtu.channel_id,
                event->mtu.value);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_MTU, DIAG_SEV_INFO,
                 0, event->mtu.value, event->mtu.conn_handle, event->mtu.channel_id);
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_mtu(event->mtu.conn_handle, event->mtu.value);
        }
        ble_diag_log_on_gap_mtu(event->mtu.conn_handle, event->mtu.value);
        ble_hid_gap_request_recovery_security_once(event->mtu.conn_handle, "mtu");
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        ESP_LOGI(TAG, "encryption change event; status=%d", event->enc_change.status);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ENCRYPT,
                 event->enc_change.status == 0 ? DIAG_SEV_INFO : DIAG_SEV_WARN,
                 event->enc_change.status == 0 ? 1 : 0,
                 (uint32_t)event->enc_change.status,
                 event->enc_change.conn_handle,
                 0);
        if (event->enc_change.status == 0) {
            ble_hid_gap_indicate_service_changed(event->enc_change.conn_handle, "encryption change");
            rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            if (rc == 0) {
                ESP_LOGI(
                    TAG,
                    "security state after encryption: encrypted=%u authenticated=%u bonded=%u key_size=%u",
                    desc.sec_state.encrypted,
                    desc.sec_state.authenticated,
                    desc.sec_state.bonded,
                    desc.sec_state.key_size);
                if (desc.sec_state.encrypted || desc.sec_state.bonded) {
                    if (ble_hid_gap_recovery_bond_delete_active()) {
                        ESP_LOGW(TAG,
                                 "recovery: terminating encrypted connection while async local bond delete is pending conn=%u",
                                 event->enc_change.conn_handle);
                        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                                 17, 0, 0, event->enc_change.conn_handle);
                        (void)ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
                        return 0;
                    }
                    ble_hid_gap_note_secure_connection(
                        event->enc_change.conn_handle,
                        "secure connection established");
                }
            } else {
                ESP_LOGW(TAG, "connection descriptor lookup failed after encryption: rc=%d", rc);
            }
            ble_hid_task_start_up();
        } else {
            ESP_LOGW(TAG, "encryption failed or connection already gone; status=%d", event->enc_change.status);
            if (ble_hid_gap_recovery_pairing_window_open()) {
                ESP_LOGW(
                    TAG,
                    "recovery: pairing encryption failure status=%d; keeping pairing advertising available for Windows retry without restarting repair",
                    event->enc_change.status);
                diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                         7, (uint32_t)event->enc_change.status, 0, event->enc_change.conn_handle);
                ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_retry_after_encrypt_fail");
                s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                s_recovery_security_failed_conn_handle = event->enc_change.conn_handle;
                int term_rc = ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                if (term_rc != 0 && term_rc != BLE_HS_ENOTCONN) {
                    ESP_LOGW(TAG,
                             "recovery: terminate failed pairing connection after encryption failure conn=%u rc=%d",
                             event->enc_change.conn_handle,
                             term_rc);
                    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                             20, (uint32_t)term_rc, 1, event->enc_change.conn_handle);
                }
                if (!ble_gap_adv_active()) {
                    esp_err_t adv_ret = ble_hid_gap_start_advertising();
                    if (adv_ret != ESP_OK) {
                        ESP_LOGW(
                            TAG,
                            "recovery: pairing encryption retry advertising restart failed ret=%s",
                            esp_err_to_name(adv_ret));
                        status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
                    }
                } else {
                    status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
                }
            } else {
                ESP_LOGW(
                    TAG,
                    "security failure outside pairing window status=%d; opening pairing reset and terminating insecure connection",
                    event->enc_change.status);
                esp_err_t repair_ret =
                    ble_hid_gap_forget_bonds_and_repair_inner(
                        s_recovery_type_controlled_pairing,
                        s_recovery_suppress_swift_pair_prompt);
                if (repair_ret != ESP_OK) {
                    ESP_LOGW(TAG,
                             "security failure repair path failed ret=%s; falling back to reconnecting LED",
                             esp_err_to_name(repair_ret));
                    status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
                }
            }
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGV(TAG, "notify_tx event; conn_handle=%d attr_handle=%d "
                "status=%d indication=%d",
                event->notify_tx.conn_handle,
                event->notify_tx.attr_handle,
                event->notify_tx.status,
                event->notify_tx.indication);
        if (event->notify_tx.indication != 0 &&
            event->notify_tx.attr_handle == ble_hid_gap_get_service_changed_val_handle()) {
            ESP_LOGI(TAG,
                     "service changed indication tx complete: conn_handle=%u attr_handle=%u status=%d",
                     event->notify_tx.conn_handle,
                     event->notify_tx.attr_handle,
                     event->notify_tx.status);
            if (event->notify_tx.status == 0) {
                ble_hid_gap_mark_service_changed_confirmed();
            } else {
                s_service_changed_queued_for_conn = false;
            }
        }
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_notify_tx(
                event->notify_tx.conn_handle,
                event->notify_tx.attr_handle,
                event->notify_tx.status,
                event->notify_tx.indication != 0);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        const uint8_t action = event->passkey.params.action;
        ESP_LOGI(
            TAG,
            "passkey action event; conn_handle=%u action=%u numcmp=%lu",
            event->passkey.conn_handle,
            action,
            (unsigned long)event->passkey.params.numcmp);
        if (action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io pkey = {0};
            pkey.action = action;
            pkey.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "passkey numeric comparison auto-accepted rc=%d", rc);
        } else if (action == BLE_SM_IOACT_DISP ||
                   action == BLE_SM_IOACT_INPUT ||
                   action == BLE_SM_IOACT_STATIC) {
            struct ble_sm_io pkey = {0};
            pkey.action = action;
            pkey.passkey = BLE_HID_GAP_PAIRING_PASSKEY;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG,
                     "passkey action satisfied with static passkey action=%u passkey=%06lu rc=%d",
                     action,
                     (unsigned long)BLE_HID_GAP_PAIRING_PASSKEY,
                     rc);
        } else if (action == BLE_SM_IOACT_NONE) {
            ESP_LOGI(TAG, "passkey action none; no app input required");
        } else if (action == BLE_SM_IOACT_OOB) {
            struct ble_sm_io pkey = {0};
            pkey.action = action;
            memset(pkey.oob, 0, sizeof(pkey.oob));
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "passkey OOB action satisfied with zero OOB rc=%d", rc);
        } else {
            ESP_LOGW(
                TAG,
                "unsupported passkey action for Listener pairing action=%u conn=%u",
                action,
                event->passkey.conn_handle);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc != 0) {
            ESP_LOGW(TAG, "repeat pairing: conn find failed rc=%d; ignoring", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_WARN,
                     0, (uint32_t)rc, event->repeat_pairing.conn_handle, 0);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        ble_store_util_delete_peer(&desc.peer_id_addr);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_INFO,
                 1, 0, event->repeat_pairing.conn_handle, 0);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    }
    return 0;
}

static void nimble_hid_on_reset(int reason)
{
    s_nimble_stack_ready = false;
    ESP_LOGW(TAG, "NimBLE host reset; reason=%d", reason);
}

static void nimble_hid_on_sync(void)
{
    int rc;
    uint8_t addr_val[6] = {0};

    esp_err_t identity_ret = ble_hid_gap_restore_random_identity_from_nvs();
    if (identity_ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "BLE random identity restore failed during host sync: %s",
            esp_err_to_name(identity_ret));
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_id_copy_addr failed: rc=%d", rc);
    } else {
        ESP_LOGI(
            TAG,
            "NimBLE host sync complete: own_addr_type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
            s_own_addr_type,
            addr_val[0],
            addr_val[1],
            addr_val[2],
            addr_val[3],
            addr_val[4],
            addr_val[5]);
    }

    s_nimble_stack_ready = true;
    s_directed_adv_pending = true;
    s_last_adv_was_directed = false;

    if (s_hid_start_event_seen) {
        ble_hid_gap_register_global_event_listener_once("nimble_sync_before_advertising");
        ble_hid_gap_start_advertising();
    } else {
        ESP_LOGI(TAG, "NimBLE host synced before HID START; advertising deferred");
    }
}

esp_err_t esp_hid_ble_gap_adv_start(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    ble_addr_t bonded_peers[4];
    int bonded_peer_count = 0;
    bool start_directed = false;
    ble_addr_t direct_peer_addr = {0};

    if (s_shutdown_quiesce) {
        ESP_LOGI(TAG, "NimBLE advertising suppressed: shutdown quiesce active");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_SUPPRESS_SHUTDOWN,
            0,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        return ESP_OK;
    }

    ble_hid_gap_keep_recovery_adv_connectable("advertising start");
    if (s_key_wake_only_advertising) {
        ESP_LOGI(TAG, "NimBLE advertising suppressed: key-wake-only idle");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_SUPPRESS_KEY_WAKE,
            0,
            ble_hid_gap_adv_active_snapshot(),
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        return ESP_OK;
    }

    if (!s_hid_start_event_seen) {
        ESP_LOGI(TAG, "NimBLE advertising deferred: HID START not seen yet");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_DEFER_HID_START,
            0,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        return ESP_OK;
    }

    if (!s_nimble_stack_ready) {
        ESP_LOGI(TAG, "NimBLE advertising deferred: host stack not synced yet");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_DEFER_STACK_SYNC,
            0,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        return ESP_OK;
    }

    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("advertising_start");
    if (conn.connected) {
        ESP_LOGI(TAG, "NimBLE advertising skipped: GAP already connected conn_handle=%u secure=%u",
                 conn.conn_handle,
                 conn.secure_connected ? 1U : 0U);
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_SKIP_CONNECTED,
            0,
            false,
            conn.conn_handle,
            DIAG_SEV_INFO);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_INFO,
                 2, 0, 1, conn.conn_handle);
        if (conn.secure_connected) {
            status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, false);
            status_led_note_ble_boot_ready("advertising_skip_connected");
        }
        return ESP_OK;
    }

    if (ble_hid_gap_recovery_bond_delete_active()) {
        ESP_LOGI(TAG, "NimBLE advertising deferred: recovery async local bond delete pending");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_DEFER_BOND_DELETE,
            0,
            ble_hid_gap_adv_active_snapshot(),
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 16, 0, 0, s_ble_gap_conn_handle);
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return ESP_OK;
    }

    if (ble_gap_adv_active()) {
        ESP_LOGI(TAG, "NimBLE advertising already active");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_ALREADY_ACTIVE,
            0,
            true,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        return ESP_OK;
    }

    esp_err_t name_ret = ble_hid_gap_refresh_configured_device_name("advertising_start");
    if (name_ret != ESP_OK) {
        ESP_LOGW(TAG, "advertising start continuing after BLE name refresh failure: %s",
                 esp_err_to_name(name_ret));
    }

    rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    const int64_t recovery_pairing_remaining_ms =
        ble_hid_gap_recovery_pairing_window_remaining_ms();
    const bool pairing_window = recovery_pairing_remaining_ms > 0;
    const int64_t first_pairing_remaining_ms = rc == 0 && !pairing_window
        ? ble_hid_gap_first_pairing_window_remaining_ms(bonded_peer_count)
        : 0;
    const bool first_pairing_window = first_pairing_remaining_ms > 0;
    if (rc == 0) {
        ESP_LOGI(TAG, "NimBLE bonded peers=%d", bonded_peer_count);
        if (bonded_peer_count > 0) {
            direct_peer_addr = bonded_peers[0];
            start_directed =
                s_directed_adv_pending && !s_low_power_advertising && !pairing_window;
        }
    } else {
        ESP_LOGW(TAG, "NimBLE bonded peer lookup failed: rc=%d", rc);
    }

    const bool type_recovery_requested =
        pairing_window && s_recovery_type_controlled_pairing;
    const int64_t recovery_swift_pair_remaining_ms =
        pairing_window
            ? ble_hid_gap_recovery_swift_pair_prompt_remaining_ms()
            : 0;
    const bool recovery_swift_pair_allowed = recovery_swift_pair_remaining_ms > 0;
    const bool swift_pair_requested = recovery_swift_pair_allowed || first_pairing_window;
    bool type_recovery_enabled = false;
    if (type_recovery_requested && !swift_pair_requested) {
        type_recovery_enabled = ble_hid_gap_configure_type_controlled_recovery_adv_fields();
    }
    const bool swift_pair_enabled =
        !type_recovery_enabled &&
        swift_pair_requested &&
        ble_hid_gap_configure_swift_pair_fields();
    if (!swift_pair_enabled && !type_recovery_enabled) {
        (void)ble_hid_gap_configure_normal_adv_fields();
    }
    ESP_LOGI(
        TAG,
        "NimBLE advertisement payload profile=%s name_len=%u recovery_window=%u type_controlled=%u suppress_swift_pair=%u recovery_swift_pair_consumed=%u first_pairing_window=%u bonded_peers=%d",
        swift_pair_enabled ? "swift_pair" : (type_recovery_enabled ? "type_recovery" : "normal"),
        (unsigned)s_adv_device_name_len,
        pairing_window ? 1u : 0u,
        s_recovery_type_controlled_pairing ? 1u : 0u,
        s_recovery_suppress_swift_pair_prompt ? 1u : 0u,
        s_recovery_swift_pair_consumed ? 1u : 0u,
        first_pairing_window ? 1u : 0u,
        bonded_peer_count);

    rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, swift_pair_enabled ? 7 : (type_recovery_enabled ? 17 : 1), 0);
        return ESP_FAIL;
    }

    rc = ble_gap_adv_rsp_set_fields(&s_scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting scan response data; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, swift_pair_enabled ? 8 : (type_recovery_enabled ? 18 : 2), 0);
        return ESP_FAIL;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    if (start_directed) {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;
        adv_params.high_duty_cycle = 1;
        rc = ble_gap_adv_start(
            s_own_addr_type,
            &direct_peer_addr,
            BLE_HS_FOREVER,
            &adv_params,
            nimble_hid_gap_event,
            NULL);
        if (rc == 0) {
            s_directed_adv_pending = false;
            s_last_adv_was_directed = true;
            ESP_LOGI(
                TAG,
                "NimBLE directed advertising started: peer_type=%u peer_addr=%02x:%02x:%02x:%02x:%02x:%02x",
                direct_peer_addr.type,
                direct_peer_addr.val[0],
                direct_peer_addr.val[1],
                direct_peer_addr.val[2],
                direct_peer_addr.val[3],
                direct_peer_addr.val[4],
                direct_peer_addr.val[5]);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_INFO,
                     1, 1, bonded_peer_count, 0);
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_START_DIRECTED,
                (uint32_t)bonded_peer_count,
                true,
                s_ble_gap_conn_handle,
                DIAG_SEV_INFO);
            status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
            status_led_note_ble_boot_ready("directed_advertising_started");
            return ESP_OK;
        }

        if (ble_hid_gap_adv_start_deferred_rc(rc)) {
            ESP_LOGW(TAG, "directed advertising start deferred while controller is in transition; rc=%d", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                     0, (uint32_t)rc, 5, 0);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "directed advertising failed, fallback to undirected; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, 3, 0);
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const uint32_t adv_min_ms = BLE_HID_GAP_FAST_ADV_MIN_MS;
    const uint32_t adv_max_ms = BLE_HID_GAP_FAST_ADV_MAX_MS;
    const int64_t swift_pair_remaining_ms =
        recovery_swift_pair_remaining_ms > 0
            ? recovery_swift_pair_remaining_ms
            : first_pairing_remaining_ms;
    const int32_t adv_duration_ms = swift_pair_enabled
        ? ble_hid_gap_swift_pair_adv_duration_ms(swift_pair_remaining_ms)
        : BLE_HS_FOREVER;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(adv_min_ms);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(adv_max_ms);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, adv_duration_ms,
                           &adv_params, nimble_hid_gap_event, NULL);
    if (rc != 0) {
        if (ble_hid_gap_adv_start_deferred_rc(rc)) {
            ESP_LOGW(TAG, "undirected advertising start deferred while controller is in transition; rc=%d", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                     0, (uint32_t)rc, 6, 0);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "error enabling undirected advertisement; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, 4, 0);
        return ESP_FAIL;
    }

    s_last_adv_was_directed = false;
    ESP_LOGI(
        TAG,
        "NimBLE undirected advertising started: low_power=%u interval_ms=%u-%u duration_ms=%ld",
        s_low_power_advertising ? 1u : 0u,
        (unsigned)adv_min_ms,
        (unsigned)adv_max_ms,
        (long)adv_duration_ms);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_INFO,
             1, s_low_power_advertising ? 2 : 0, bonded_peer_count, 0);
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_START_UNDIRECTED,
        s_low_power_advertising ? 1U : 0U,
        true,
        s_ble_gap_conn_handle,
        DIAG_SEV_INFO);
    if (!s_low_power_advertising) {
        status_led_set_ble_state(
            pairing_window
                ? STATUS_LED_BLE_PAIRING
                : STATUS_LED_BLE_RECONNECTING,
            false);
        status_led_note_ble_boot_ready("undirected_advertising_started");
    }
    return ESP_OK;
}

/*
 * CONTROLLER INIT
 */

static esp_err_t init_low_level(uint8_t mode)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
    bt_cfg.mode = mode;
#endif
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_ERROR, 1, (uint32_t)ret, 0, 0);
        return ret;
    }
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_ERROR, 2, (uint32_t)ret, 0, 0);
        return ret;
    }

    ret = esp_bt_controller_enable(mode);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_ERROR, 3, (uint32_t)ret, 0, 0);
        return ret;
    }

#if CONFIG_BT_CTRL_MODEM_SLEEP
    ret = esp_bt_sleep_enable();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_bt_sleep_enable failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_WARN, 6, (uint32_t)ret, mode, 0);
    } else {
        ESP_LOGI(TAG, "Bluetooth controller modem sleep enabled");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_INFO, 6, 0, mode, 0);
    }
#else
    ESP_LOGI(TAG, "Bluetooth controller modem sleep disabled for Windows HID pairing stability");
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_INFO, 6, 2, mode, 0);
#endif

    ret = esp_nimble_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_ERROR, 4, (uint32_t)ret, 0, 0);
        return ret;
    }
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CTRL_INIT, DIAG_SEV_INFO, 5, 0, mode, 0);

    ble_hs_cfg.sync_cb = nimble_hid_on_sync;
    ble_hs_cfg.reset_cb = nimble_hid_on_reset;

    return ret;
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    esp_err_t ret;
    if (!mode || mode > ESP_BT_MODE_BTDM) {
        ESP_LOGE(TAG, "Invalid mode given!");
        return ESP_FAIL;
    }

    ret = init_low_level(mode);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t ble_hid_gap_init(void)
{
    return esp_hid_gap_init(HIDD_BLE_MODE);
}

void ble_hid_gap_set_audio_enabled(bool enabled)
{
    s_audio_enabled = enabled;
    ESP_LOGI(TAG, "audio GAP integration enabled=%u", enabled ? 1u : 0u);
}

esp_err_t ble_hid_gap_configure_advertising(uint16_t appearance, const char *device_name)
{
    return esp_hid_ble_gap_adv_init(appearance, device_name);
}

esp_err_t ble_hid_gap_start_advertising(void)
{
    return esp_hid_ble_gap_adv_start();
}

static void ble_hid_gap_recovery_bond_delete_set_state(
    bool pending,
    bool in_progress,
    TaskHandle_t task_handle)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_recovery_bond_delete_pending = pending;
    s_recovery_bond_delete_in_progress = in_progress;
    s_recovery_bond_delete_task_handle = task_handle;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
}

static void ble_hid_gap_recovery_bond_delete_task(void *arg)
{
    const uint32_t initial_bond_count = (uint32_t)(uintptr_t)arg;
    uint32_t waited_ms = 0;

    vTaskDelay(pdMS_TO_TICKS(BLE_HID_GAP_RECOVERY_BOND_DELETE_SETTLE_MS));
    while (waited_ms < BLE_HID_GAP_RECOVERY_BOND_DELETE_WAIT_MS) {
        if (!ble_hid_gap_connection_snapshot().connected) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(BLE_HID_GAP_RECOVERY_BOND_DELETE_POLL_MS));
        waited_ms += BLE_HID_GAP_RECOVERY_BOND_DELETE_POLL_MS;
    }

    if (ble_hid_gap_connection_snapshot().connected) {
        ESP_LOGW(TAG,
                 "recovery: async local bond delete timed out waiting for disconnect initial_bonds=%lu waited_ms=%lu",
                 (unsigned long)initial_bond_count,
                 (unsigned long)waited_ms);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 15, 1, initial_bond_count, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_disconnect_timeout");
        ble_hid_gap_recovery_bond_delete_set_state(false, false, NULL);
        vTaskDelete(NULL);
        return;
    }

    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_recovery_bond_delete_pending = false;
    s_recovery_bond_delete_in_progress = true;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    if (ble_gap_adv_active()) {
        int stop_rc = ble_gap_adv_stop();
        if (stop_rc != 0) {
            ESP_LOGW(TAG, "recovery: advertising stop before async bond delete failed rc=%d", stop_rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     16, (uint32_t)stop_rc, initial_bond_count, s_ble_gap_conn_handle);
        }
    }

    ble_addr_t bonded_peers[8];
    int bonded_peer_count = 0;
    int lookup_rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    int first_delete_rc = 0;
    int deleted_count = 0;
    if (lookup_rc == 0) {
        for (int index = 0; index < bonded_peer_count; ++index) {
            int delete_rc = ble_store_util_delete_peer(&bonded_peers[index]);
            if (delete_rc == 0) {
                ++deleted_count;
            } else {
                if (first_delete_rc == 0) {
                    first_delete_rc = delete_rc;
                }
                ESP_LOGW(
                    TAG,
                    "recovery: async local bond delete failed peer_index=%d rc=%d",
                    index,
                    delete_rc);
            }
        }
    }

    const bool delete_ok = lookup_rc == 0 && first_delete_rc == 0;
    ESP_LOGW(
        TAG,
        "recovery: async local bond delete complete lookup_rc=%d bonded_peers=%d deleted=%d initial_bonds=%lu",
        lookup_rc,
        bonded_peer_count,
        deleted_count,
        (unsigned long)initial_bond_count);
    diag_log(
        DIAG_SRC_BLE_GAP,
        DIAG_GAP_RECOVERY,
        delete_ok ? DIAG_SEV_INFO : DIAG_SEV_WARN,
        15,
        (uint32_t)(lookup_rc != 0 ? lookup_rc : first_delete_rc),
        (uint32_t)deleted_count,
        s_ble_gap_conn_handle);

    ble_hid_gap_recovery_bond_delete_set_state(false, false, NULL);

    if (!delete_ok) {
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_bond_delete_failed");
        vTaskDelete(NULL);
        return;
    }

    if (!ble_hid_gap_recovery_pairing_window_open()) {
        ESP_LOGI(TAG, "recovery: pairing window closed before async bond delete finished");
        vTaskDelete(NULL);
        return;
    }

    if (s_native_recovery_identity_rotate_pending) {
        esp_err_t identity_ret =
            ble_hid_gap_rotate_native_recovery_identity("async_bond_delete_complete");
        if (identity_ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "recovery: native identity rotation after async local bond delete failed: %s",
                esp_err_to_name(identity_ret));
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
            vTaskDelete(NULL);
            return;
        }
    }

    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;
    esp_err_t adv_ret = ble_hid_gap_start_advertising();
    if (adv_ret != ESP_OK) {
        ESP_LOGE(TAG, "recovery: advertising restart after async bond delete failed: %s",
                 esp_err_to_name(adv_ret));
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 3, (uint32_t)adv_ret, (uint32_t)deleted_count, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_adv_restart_failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(
        TAG,
        "recovery: pairing reset complete after async local bond delete, BLE identity ready for re-pair native_rotated=%u",
        s_native_recovery_random_identity_active ? 1u : 0u);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE);
    ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_reset_complete");
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             4, 0, (uint32_t)deleted_count, s_ble_gap_conn_handle);
    vTaskDelete(NULL);
}

static esp_err_t ble_hid_gap_schedule_recovery_bond_delete(uint32_t bonded_peer_count)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    if (s_recovery_bond_delete_pending || s_recovery_bond_delete_in_progress) {
        portEXIT_CRITICAL(&s_ble_gap_state_lock);
        ESP_LOGI(TAG, "recovery: async local bond delete already scheduled");
        return ESP_OK;
    }
    s_recovery_bond_delete_pending = true;
    s_recovery_bond_delete_in_progress = false;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    TaskHandle_t task_handle = NULL;
    BaseType_t task_ok = xTaskCreate(
        ble_hid_gap_recovery_bond_delete_task,
        "ble_bond_del",
        BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_STACK,
        (void *)(uintptr_t)bonded_peer_count,
        BLE_HID_GAP_RECOVERY_BOND_DELETE_TASK_PRIO,
        &task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "recovery: async local bond delete task create failed");
        ble_hid_gap_recovery_bond_delete_set_state(false, false, NULL);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 15, 2, bonded_peer_count, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_bond_delete_task_failed");
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_recovery_bond_delete_task_handle = task_handle;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    ESP_LOGW(TAG,
             "recovery: async local bond delete scheduled bonded_peers=%lu; advertising will wait until delete completes",
             (unsigned long)bonded_peer_count);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
             15, 0, bonded_peer_count, s_ble_gap_conn_handle);
    return ESP_OK;
}

esp_err_t ble_hid_gap_mark_stack_ready(void)
{
    s_hid_start_event_seen = true;
    if (!s_nimble_stack_ready) {
        ESP_LOGI(TAG, "HID START received; waiting for NimBLE host sync before advertising");
        return ESP_OK;
    }
    ble_hid_gap_register_global_event_listener_once("hid_start_before_advertising");
    return ble_hid_gap_start_advertising();
}

static esp_err_t ble_hid_gap_forget_bonds_and_repair_inner(
    bool type_controlled_request,
    bool suppress_swift_pair_prompt)
{
    s_shutdown_quiesce = false;
    s_low_power_advertising = false;
    s_key_wake_only_advertising = false;

    ble_addr_t bonded_peers[8];
    int bonded_peer_count = 0;
    int rc = ble_store_util_bonded_peers(
        bonded_peers,
        &bonded_peer_count,
        sizeof(bonded_peers) / sizeof(bonded_peers[0]));
    if (rc != 0) {
        ESP_LOGE(TAG, "recovery: bonded peer lookup failed rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 1, (uint32_t)rc, 0, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_bond_lookup_failed");
        return ESP_FAIL;
    }

    bool refresh_pairing_window =
        ble_hid_gap_recovery_pairing_window_open() &&
        bonded_peer_count == 0 &&
        !s_ble_gap_connected;
    if (refresh_pairing_window && (type_controlled_request || s_recovery_type_controlled_pairing)) {
        ESP_LOGW(TAG, "recovery: pairing window already active; refreshing advertising with stable BLE identity");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 6, 0, 0, s_ble_gap_conn_handle);
        ble_hid_gap_open_recovery_pairing_window(
            type_controlled_request || s_recovery_type_controlled_pairing,
            suppress_swift_pair_prompt || s_recovery_suppress_swift_pair_prompt);
        s_directed_adv_pending = false;
        s_last_adv_was_directed = false;

        if (ble_gap_adv_active()) {
            rc = ble_gap_adv_stop();
            if (rc != 0) {
                ESP_LOGW(TAG, "recovery: pairing window advertising refresh stop failed rc=%d", rc);
                diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                         6, (uint32_t)rc, 0, s_ble_gap_conn_handle);
            }
        }

        esp_err_t adv_ret = ble_hid_gap_start_advertising();
        if (adv_ret != ESP_OK) {
            ESP_LOGE(TAG, "recovery: pairing window advertising refresh failed: %s", esp_err_to_name(adv_ret));
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                     6, (uint32_t)adv_ret, 0, s_ble_gap_conn_handle);
            return adv_ret;
        }

        ESP_LOGW(TAG, "recovery: pairing window refreshed with stable BLE identity, device remains discoverable for re-pair");
        status_led_notify_ble_repairing_for_ms("ble_recovery_refresh_pairing", (uint32_t)BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS);
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return ESP_OK;
    }
    if (refresh_pairing_window) {
        ESP_LOGW(TAG, "recovery: Swift Pair window already active; restarting session so Windows may show a fresh pairing notification");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 16, 0, 0, s_ble_gap_conn_handle);
        ble_hid_gap_close_recovery_pairing_window("restart_swift_pair_session");
        if (ble_gap_adv_active()) {
            rc = ble_gap_adv_stop();
            if (rc != 0) {
                ESP_LOGW(TAG, "recovery: Swift Pair session restart stop failed rc=%d; restart will continue", rc);
                diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                         16, (uint32_t)rc, 0, s_ble_gap_conn_handle);
            }
        }
    }

    const bool type_link_ready_before_recovery = ble_audio_stream_is_type_link_ready();
    const bool type_host_recent_before_recovery =
        ble_audio_stream_was_type_host_recently_seen();
    const bool type_controlled_recovery =
        type_controlled_request ||
        type_link_ready_before_recovery ||
        type_host_recent_before_recovery;
    ESP_LOGW(
        TAG,
        "recovery: opening pairing reset window bonded_peers=%d bond_delete=async_after_disconnect type_controlled=%u suppress_swift_pair=%u type_link_ready_before_recovery=%u type_host_recent=%u type_request=%u",
        bonded_peer_count,
        type_controlled_recovery ? 1u : 0u,
        suppress_swift_pair_prompt ? 1u : 0u,
        type_link_ready_before_recovery ? 1u : 0u,
        type_host_recent_before_recovery ? 1u : 0u,
        type_controlled_request ? 1u : 0u);
    status_led_notify_ble_repairing_for_ms("ble_recovery_clear_bonds", (uint32_t)BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
             1, 0, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);

    ble_hid_gap_open_recovery_pairing_window(
        type_controlled_recovery,
        suppress_swift_pair_prompt);
    ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_window_open");
    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;

    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("recovery_pairing_reset");
    if (!type_controlled_recovery) {
        if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_hid_gap_defer_native_recovery_identity_rotation("recovery_pairing_reset_connected");
        } else {
            esp_err_t identity_ret =
                ble_hid_gap_rotate_native_recovery_identity("recovery_pairing_reset");
            if (identity_ret != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "recovery: native Windows pairing identity rotation failed: %s",
                    esp_err_to_name(identity_ret));
                status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
                return identity_ret;
            }
        }
    } else {
        s_native_recovery_identity_rotate_pending = false;
    }

    if (bonded_peer_count > 0) {
        esp_err_t delete_ret = ble_hid_gap_schedule_recovery_bond_delete((uint32_t)bonded_peer_count);
        if (delete_ret != ESP_OK) {
            ESP_LOGE(TAG, "recovery: async local bond delete scheduling failed: %s", esp_err_to_name(delete_ret));
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                     15, (uint32_t)delete_ret, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
            return delete_ret;
        }
    }

    if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        s_recovery_waiting_for_disconnect = true;
        rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == 0 || rc == BLE_HS_EALREADY) {
            ESP_LOGW(
                TAG,
                "recovery: active BLE connection %s for re-pair; %s identity will advertise after disconnect and async local bond delete",
                rc == 0 ? "terminating" : "termination already in progress",
                type_controlled_recovery ? "stable Type-controlled" : "rotated native Windows");
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     2, (uint32_t)rc, (uint32_t)bonded_peer_count, conn.conn_handle);
            return ESP_OK;
        }
        s_recovery_waiting_for_disconnect = false;
        ESP_LOGW(TAG, "recovery: BLE terminate failed rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 2, (uint32_t)rc, (uint32_t)bonded_peer_count, conn.conn_handle);
        if (rc != BLE_HS_ENOTCONN && rc != BLE_HS_EINVAL) {
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "recovery: terminate reported no active connection; clearing stale GAP state and continuing to pairable advertising");
        ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
        power_manager_set_ble_connected(false);
    }

    if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "recovery: advertising stop failed rc=%d; restart will continue", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     3, (uint32_t)rc, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
        }
    }

    esp_err_t adv_ret = ble_hid_gap_start_advertising();
    if (adv_ret != ESP_OK) {
        ESP_LOGE(TAG, "recovery: advertising restart failed: %s", esp_err_to_name(adv_ret));
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 3, (uint32_t)adv_ret, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_adv_restart_failed");
        return adv_ret;
    }
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             3, 0, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);

    if (ble_hid_gap_recovery_bond_delete_active()) {
        ESP_LOGW(
            TAG,
            "recovery: pairing reset waiting for async local bond delete before BLE identity advertising starts native_rotate_pending=%u",
            s_native_recovery_identity_rotate_pending ? 1u : 0u);
        ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_waiting_for_bond_delete");
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "recovery: pairing reset complete, BLE identity ready and device is discoverable for re-pair native_rotated=%u",
        s_native_recovery_random_identity_active ? 1u : 0u);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE);
    ble_hid_gap_hold_recovery_pairing_led("ble_recovery_pairing_reset_complete");
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             4, 0, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
    return ESP_OK;
}

esp_err_t ble_hid_gap_forget_bonds_and_repair(void)
{
    return ble_hid_gap_forget_bonds_and_repair_inner(false, false);
}

esp_err_t ble_hid_gap_forget_bonds_and_repair_type_controlled(void)
{
    return ble_hid_gap_forget_bonds_and_repair_inner(true, false);
}

esp_err_t ble_hid_gap_forget_bonds_and_repair_type_controlled_silent(void)
{
    return ble_hid_gap_forget_bonds_and_repair_inner(true, true);
}

bool ble_hid_gap_is_connected(void)
{
    return ble_hid_gap_reconcile_connection_snapshot("is_connected").connected;
}

bool ble_hid_gap_is_securely_connected(void)
{
    return ble_hid_gap_reconcile_connection_snapshot("is_securely_connected").secure_connected;
}

bool ble_hid_gap_is_recovery_pairing_window_open(void)
{
    return ble_hid_gap_recovery_pairing_window_open();
}

esp_err_t ble_hid_gap_set_low_power_advertising(bool enabled)
{
    if (enabled && ble_hid_gap_recovery_pairing_needs_connectable_adv()) {
        ble_hid_gap_keep_recovery_adv_connectable("low-power request");
        return ESP_OK;
    }

    if (s_low_power_advertising == enabled) {
        if (!enabled) {
            s_key_wake_only_advertising = false;
        }
        return ESP_OK;
    }

    s_low_power_advertising = enabled;
    if (!enabled) {
        s_key_wake_only_advertising = false;
    }
    ESP_LOGI(TAG, "low-power advertising=%u", enabled ? 1u : 0u);

    const bool adv_active = ble_hid_gap_adv_active_snapshot();
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_LOW_POWER_SET,
        enabled ? 1U : 0U,
        adv_active,
        s_ble_gap_conn_handle,
        DIAG_SEV_INFO);

    if (!s_nimble_stack_ready || s_ble_gap_connected || !adv_active) {
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "advertising restart for low-power mode failed to stop: rc=%d", rc);
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_STOP_FOR_RESTART,
            (uint32_t)rc,
            ble_hid_gap_adv_active_snapshot(),
            s_ble_gap_conn_handle,
            DIAG_SEV_WARN);
        return ESP_FAIL;
    }
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_STOP_FOR_RESTART,
        enabled ? 1U : 0U,
        false,
        s_ble_gap_conn_handle,
        DIAG_SEV_INFO);

    return ble_hid_gap_start_advertising();
}

esp_err_t ble_hid_gap_stop_advertising_for_key_wake(void)
{
    if (ble_hid_gap_recovery_pairing_needs_connectable_adv()) {
        ble_hid_gap_keep_recovery_adv_connectable("key-wake-only stop");
        ESP_LOGI(TAG, "BLE key-wake-only advertising stop deferred: recovery pairing window active");
        if (!ble_gap_adv_active()) {
            return ble_hid_gap_start_advertising();
        }
        return ESP_OK;
    }

    s_low_power_advertising = true;
    s_key_wake_only_advertising = true;
    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;
    ESP_LOGI(TAG, "BLE advertising stopped: key-wake-only idle");

    const bool adv_active = ble_hid_gap_adv_active_snapshot();
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_KEY_WAKE_STOP,
        0,
        adv_active,
        s_ble_gap_conn_handle,
        DIAG_SEV_INFO);

    if (!s_nimble_stack_ready || s_ble_gap_connected || !adv_active) {
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "key-wake-only advertising stop failed: rc=%d", rc);
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_KEY_WAKE_STOP,
            (uint32_t)rc,
            ble_hid_gap_adv_active_snapshot(),
            s_ble_gap_conn_handle,
            DIAG_SEV_WARN);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void)
{
    s_shutdown_quiesce = true;
    s_low_power_advertising = true;
    s_key_wake_only_advertising = true;
    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;

    if (!s_nimble_stack_ready) {
        ESP_LOGW(TAG, "shutdown BLE disconnect skipped: NimBLE stack is not ready");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_SHUTDOWN_PREPARE,
            (uint32_t)ESP_ERR_INVALID_STATE,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_WARN);
        return ESP_ERR_INVALID_STATE;
    }

    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    const bool adv_active = ble_hid_gap_adv_active_snapshot();
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_SHUTDOWN_PREPARE,
        conn.connected ? 1U : (adv_active ? 2U : 0U),
        adv_active,
        conn.conn_handle,
        DIAG_SEV_INFO);
    if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGW(TAG, "shutdown BLE disconnect failed: conn_handle=%u rc=%d", conn.conn_handle, rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     2, (uint32_t)rc, 0, conn.conn_handle);
            return ESP_FAIL;
        }

        ESP_LOGW(TAG, "shutdown BLE disconnect requested: conn_handle=%u", conn.conn_handle);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                 2, 0, 0, conn.conn_handle);
        return ESP_OK;
    }

    if (ble_gap_adv_active()) {
        int rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "shutdown BLE advertising stop failed: rc=%d", rc);
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_STOP_FOR_SHUTDOWN,
                (uint32_t)rc,
                ble_hid_gap_adv_active_snapshot(),
                s_ble_gap_conn_handle,
                DIAG_SEV_WARN);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     3, (uint32_t)rc, 0, s_ble_gap_conn_handle);
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "shutdown BLE advertising stopped");
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_STOP_FOR_SHUTDOWN,
            0,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 3, 0, 0, s_ble_gap_conn_handle);
    }

    return ESP_OK;
}

esp_err_t ble_hid_gap_request_reconnect(void)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("reconnect_request");
    if (conn.connected) {
        return ble_hid_gap_request_active_connection();
    }

    const bool adv_active = s_nimble_stack_ready && ble_gap_adv_active();
    const uint32_t state_flags =
        (adv_active ? 1U : 0U) |
        (s_low_power_advertising ? 2U : 0U) |
        (s_directed_adv_pending ? 4U : 0U) |
        (s_key_wake_only_advertising ? 8U : 0U);

    ESP_LOGI(TAG,
             "BLE reconnect requested: adv_active=%u low_power_adv=%u directed_pending=%u key_wake_only=%u",
             adv_active ? 1U : 0U,
             s_low_power_advertising ? 1U : 0U,
             s_directed_adv_pending ? 1U : 0U,
             s_key_wake_only_advertising ? 1U : 0U);
    diag_log(DIAG_SRC_BLE_GAP,
             DIAG_GAP_RECOVERY,
             DIAG_SEV_INFO,
             7,
             0,
             state_flags,
             s_ble_gap_conn_handle);
    ble_hid_gap_log_adv_state(
        BLE_HID_GAP_ADV_STATE_RECONNECT_REQUEST,
        0,
        adv_active,
        s_ble_gap_conn_handle,
        DIAG_SEV_INFO);

    if (adv_active && !s_low_power_advertising && !s_key_wake_only_advertising) {
        ESP_LOGI(TAG, "BLE reconnect request kept existing active advertising");
        status_led_set_ble_state(
            ble_hid_gap_recovery_pairing_window_open()
                ? STATUS_LED_BLE_PAIRING
                : STATUS_LED_BLE_RECONNECTING,
            false);
        return ESP_OK;
    }

    s_shutdown_quiesce = false;
    s_low_power_advertising = false;
    s_key_wake_only_advertising = false;
    s_directed_adv_pending = true;
    s_last_adv_was_directed = false;

    if (!s_nimble_stack_ready || !s_hid_start_event_seen) {
        ESP_LOGW(TAG,
                 "Cannot restart BLE advertising yet: nimble_ready=%u hid_started=%u",
                 s_nimble_stack_ready ? 1U : 0U,
                 s_hid_start_event_seen ? 1U : 0U);
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_RECONNECT_REQUEST,
            (uint32_t)ESP_ERR_INVALID_STATE,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_WARN);
        return ESP_ERR_INVALID_STATE;
    }

    if (adv_active) {
        const int rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to stop BLE advertising before reconnect request: rc=%d", rc);
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_STOP_FOR_RECONNECT,
                (uint32_t)rc,
                ble_hid_gap_adv_active_snapshot(),
                s_ble_gap_conn_handle,
                DIAG_SEV_WARN);
            return ESP_FAIL;
        }
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_STOP_FOR_RECONNECT,
            0,
            false,
            s_ble_gap_conn_handle,
            DIAG_SEV_INFO);
    }

    return ble_hid_gap_start_advertising();
}

esp_err_t ble_hid_gap_request_low_power_connection(void)
{
    if (ble_hid_gap_ec11_fast_recording_armed()) {
        ESP_LOGI(
            TAG,
            "low-power idle connection retained active: e11r fast recording is armed");
        return ble_hid_gap_request_active_connection();
    }

    ble_hid_gap_set_active_connection_required(false);
    return ble_hid_gap_request_connection_params(
        "low-power idle",
        BLE_HID_GAP_LOW_POWER_ITVL_MIN,
        BLE_HID_GAP_LOW_POWER_ITVL_MAX,
        BLE_HID_GAP_LOW_POWER_LATENCY,
        BLE_HID_GAP_LOW_POWER_SUPERVISION_TIMEOUT,
        BLE_HID_CONN_PARAM_MODE_LOW_POWER);
}

static esp_err_t ble_hid_gap_request_active_connection_once(void)
{
    esp_err_t params_ret = ble_hid_gap_request_connection_params(
        "active",
        BLE_HID_GAP_ACTIVE_ITVL_MIN,
        BLE_HID_GAP_ACTIVE_ITVL_MAX,
        BLE_HID_GAP_ACTIVE_LATENCY,
        BLE_HID_GAP_ACTIVE_SUPERVISION_TIMEOUT,
        BLE_HID_CONN_PARAM_MODE_ACTIVE);
    if (params_ret == ESP_OK && ble_hid_gap_active_connection_applied()) {
        (void)ble_hid_gap_request_preferred_2m_phy("active audio");
    }
    return params_ret;
}

static TickType_t ble_hid_gap_conn_param_retry_delay_ticks(void)
{
    TickType_t retry_not_before_tick = 0;
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    retry_not_before_tick = s_conn_param_retry_not_before_tick;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    TickType_t now = xTaskGetTickCount();
    if (retry_not_before_tick == 0 ||
        ble_hid_gap_tick_reached(now, retry_not_before_tick)) {
        return 0;
    }
    return retry_not_before_tick - now;
}

static void ble_hid_gap_active_connection_request_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    if (delay_ms != 0U) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    TickType_t deadline = xTaskGetTickCount() +
        pdMS_TO_TICKS(BLE_HID_GAP_ACTIVE_PROMOTION_TIMEOUT_MS);
    esp_err_t ret = ESP_OK;
    bool confirmed = false;
    bool awaiting_gap_completion = false;
    while (ble_hid_gap_active_connection_required()) {
        ret = ble_hid_gap_request_active_connection_once();
        if (ret == ESP_OK && ble_hid_gap_active_connection_applied()) {
            confirmed = true;
            break;
        }
        if (ble_hid_gap_conn_param_request_is_pending()) {
            awaiting_gap_completion = true;
            break;
        }
        TickType_t now = xTaskGetTickCount();
        if (ble_hid_gap_tick_reached(now, deadline)) {
            break;
        }
        TickType_t sleep_ticks = ble_hid_gap_conn_param_retry_delay_ticks();
        if (sleep_ticks == 0) {
            sleep_ticks = pdMS_TO_TICKS(BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS);
        }
        TickType_t remaining_ticks = deadline - now;
        if (sleep_ticks > remaining_ticks) {
            sleep_ticks = remaining_ticks;
        }
        vTaskDelay(sleep_ticks);
    }
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_active_connection_request_pending = false;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    if (confirmed) {
        ESP_LOGI(TAG,
                 "active connection promotion confirmed: retry_ms=%u target_itvl=%u-%u latency=%u",
                 (unsigned)BLE_HID_GAP_ACTIVE_PROMOTION_RETRY_MS,
                 BLE_HID_GAP_ACTIVE_ITVL_MIN,
                 BLE_HID_GAP_ACTIVE_ITVL_MAX,
                 BLE_HID_GAP_ACTIVE_LATENCY);
    } else if (awaiting_gap_completion) {
        ESP_LOGI(TAG,
                 "active connection promotion awaiting GAP completion event");
    } else if (ble_hid_gap_active_connection_required()) {
        ESP_LOGW(TAG,
                 "active connection promotion timed out: ret=%s timeout_ms=%u",
                 esp_err_to_name(ret),
                 (unsigned)BLE_HID_GAP_ACTIVE_PROMOTION_TIMEOUT_MS);
    }
    vTaskDelete(NULL);
}

static esp_err_t ble_hid_gap_schedule_active_connection_with_delay(
    uint32_t delay_ms,
    const char *reason)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("active_connection_promotion");
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_ble_gap_state_lock);
    bool already_pending = s_active_connection_request_pending;
    if (!already_pending) {
        s_active_connection_request_pending = true;
    }
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    if (already_pending) {
        return ESP_OK;
    }

    BaseType_t started = xTaskCreate(
        ble_hid_gap_active_connection_request_task,
        "ble_active_link",
        BLE_HID_GAP_ACTIVE_REQUEST_TASK_STACK_BYTES,
        (void *)(uintptr_t)delay_ms,
        tskIDLE_PRIORITY + 2,
        NULL);
    if (started != pdPASS) {
        portENTER_CRITICAL(&s_ble_gap_state_lock);
        s_active_connection_request_pending = false;
        portEXIT_CRITICAL(&s_ble_gap_state_lock);
        ESP_LOGW(TAG, "deferred active connection request task could not start");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "active connection promotion scheduled: conn=%u delay_ms=%u reason=%s",
             conn.conn_handle,
             (unsigned)delay_ms,
             reason != NULL ? reason : "unspecified");
    return ESP_OK;
}

esp_err_t ble_hid_gap_request_active_connection(void)
{
    ble_hid_gap_set_active_connection_required(true);
    esp_err_t params_ret = ble_hid_gap_request_active_connection_once();
    if (params_ret == ESP_OK && !ble_hid_gap_active_connection_applied()) {
        (void)ble_hid_gap_schedule_active_connection_with_delay(
            0U,
            "active request completed before parameters were applied");
    }
    return params_ret;
}

void ble_hid_gap_set_ec11_fast_recording_enabled(bool enabled, const char *reason)
{
    bool changed = false;
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    changed = s_ec11_fast_recording_armed != enabled;
    s_ec11_fast_recording_armed = enabled;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    if (!changed) {
        return;
    }

    ESP_LOGI(
        TAG,
        "e11r fast recording %s reason=%s",
        enabled ? "armed" : "disarmed",
        reason != NULL ? reason : "unspecified");

    if (enabled) {
        esp_err_t ret = ble_hid_gap_request_active_connection();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG,
                     "e11r fast recording arm could not request active connection: %s",
                     esp_err_to_name(ret));
        }
        return;
    }

    if (power_manager_get_state() == POWER_MANAGER_STATE_CONNECTED_IDLE) {
        (void)ble_hid_gap_request_low_power_connection();
    }
}

esp_err_t ble_hid_gap_schedule_active_connection(void)
{
    ble_hid_gap_set_active_connection_required(true);
    return ble_hid_gap_schedule_active_connection_with_delay(
        BLE_HID_GAP_ACTIVE_REQUEST_DEFER_MS,
        "deferred active request");
}

bool ble_hid_gap_active_connection_applied(void)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("active_connection_applied");
    return conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE &&
           ble_hid_gap_conn_desc_matches_params(
               conn.conn_handle,
               BLE_HID_GAP_ACTIVE_ITVL_MIN,
               BLE_HID_GAP_ACTIVE_ITVL_MAX,
               BLE_HID_GAP_ACTIVE_LATENCY);
}

bool ble_hid_gap_ota_connection_ready(void)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("ota_connection_is_fast");
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(conn.conn_handle, &desc) == 0 &&
           desc.conn_latency == 0 && desc.conn_itvl <= 12U;
}

static void ble_hid_gap_ota_reconnect_task(void *arg)
{
    uint16_t requested_conn_handle = (uint16_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_HID_GAP_OTA_RECONNECT_DEFER_MS));

    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("ota_reconnect_handoff");
    int rc = 0;
    if (!conn.connected || conn.conn_handle != requested_conn_handle) {
        ESP_LOGI(TAG, "OTA reconnect handoff already completed: requested_conn=%u current_conn=%u connected=%u",
                 requested_conn_handle, conn.conn_handle, conn.connected ? 1U : 0U);
    } else if (ble_hid_gap_ota_connection_ready()) {
        ESP_LOGI(TAG, "OTA reconnect handoff kept existing fast connection: conn=%u",
                 conn.conn_handle);
    } else {
        rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ESP_LOGI(TAG, "OTA reconnect handoff terminated low-power connection: conn=%u rc=%d",
                 conn.conn_handle, rc);
    }

    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_ota_reconnect_request_pending = false;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    vTaskDelete(NULL);
}

esp_err_t ble_hid_gap_schedule_ota_reconnect(void)
{
    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("ota_reconnect_schedule");
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_ble_gap_state_lock);
    bool already_pending = s_ota_reconnect_request_pending;
    if (!already_pending) {
        s_ota_reconnect_request_pending = true;
    }
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    if (already_pending) {
        return ESP_OK;
    }

    BaseType_t started = xTaskCreate(
        ble_hid_gap_ota_reconnect_task,
        "ble_ota_relink",
        BLE_HID_GAP_OTA_RECONNECT_TASK_STACK_BYTES,
        (void *)(uintptr_t)conn.conn_handle,
        tskIDLE_PRIORITY + 2,
        NULL);
    if (started != pdPASS) {
        portENTER_CRITICAL(&s_ble_gap_state_lock);
        s_ota_reconnect_request_pending = false;
        portEXIT_CRITICAL(&s_ble_gap_state_lock);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA reconnect handoff scheduled: conn=%u delay_ms=%u",
             conn.conn_handle, (unsigned)BLE_HID_GAP_OTA_RECONNECT_DEFER_MS);
    return ESP_OK;
}

esp_err_t ble_hid_gap_apply_pending_ble_name(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_recovery_pairing_window_active = false;
    s_recovery_type_controlled_pairing = false;
    s_recovery_suppress_swift_pair_prompt = false;
    s_recovery_waiting_for_disconnect = false;
    s_recovery_swift_pair_consumed = true;
    s_recovery_security_request_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_recovery_security_failed_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_recovery_pairing_window_opened_at_ms = 0;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);

    esp_err_t name_ret = ble_hid_gap_refresh_configured_device_name("ble_name_apply");
    if (name_ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE name apply failed while refreshing configured identity: %s",
                 esp_err_to_name(name_ret));
        diag_log(DIAG_SRC_BLE_GAP,
                 DIAG_GAP_RECOVERY,
                 DIAG_SEV_WARN,
                 18,
                 (uint32_t)name_ret,
                 ble_hid_gap_adv_state_flags(ble_hid_gap_adv_active_snapshot()),
                 s_ble_gap_conn_handle);
        return name_ret;
    }

    ble_hid_gap_connection_snapshot_t conn =
        ble_hid_gap_reconcile_connection_snapshot("ble_name_apply");
    const bool adv_active = s_nimble_stack_ready && ble_gap_adv_active();
    ESP_LOGI(TAG,
             "BLE name apply requested: connected=%u conn_handle=%u adv_active=%u low_power_adv=%u key_wake_only=%u",
             conn.connected ? 1U : 0U,
             conn.conn_handle,
             adv_active ? 1U : 0U,
             s_low_power_advertising ? 1U : 0U,
             s_key_wake_only_advertising ? 1U : 0U);
    diag_log(DIAG_SRC_BLE_GAP,
             DIAG_GAP_RECOVERY,
             DIAG_SEV_INFO,
             18,
             conn.connected ? 1U : 0U,
             ble_hid_gap_adv_state_flags(adv_active),
             conn.conn_handle);

    s_shutdown_quiesce = false;
    s_low_power_advertising = false;
    s_key_wake_only_advertising = false;
    s_directed_adv_pending = true;
    s_last_adv_was_directed = false;
    status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);

    if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == 0) {
            ESP_LOGI(TAG, "BLE name apply terminating active connection so bonded host reconnects with refreshed name");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "BLE name apply terminate failed rc=%d; attempting advertising restart path", rc);
        if (rc != BLE_HS_ENOTCONN && rc != BLE_HS_EINVAL) {
            return ESP_FAIL;
        }
        ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
        power_manager_set_ble_connected(false);
        conn = ble_hid_gap_connection_snapshot();
    }

    if (!s_nimble_stack_ready || !s_hid_start_event_seen) {
        ESP_LOGW(TAG,
                 "BLE name apply cannot restart advertising yet: nimble_ready=%u hid_started=%u",
                 s_nimble_stack_ready ? 1U : 0U,
                 s_hid_start_event_seen ? 1U : 0U);
        return ESP_ERR_INVALID_STATE;
    }

    if (adv_active) {
        int stop_rc = ble_gap_adv_stop();
        if (stop_rc != 0) {
            ESP_LOGW(TAG, "BLE name apply advertising stop failed rc=%d", stop_rc);
            return ESP_FAIL;
        }
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_STOP_FOR_RESTART,
            0,
            false,
            conn.conn_handle,
            DIAG_SEV_INFO);
    }

    return ble_hid_gap_start_advertising();
}
