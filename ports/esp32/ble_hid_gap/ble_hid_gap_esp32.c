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

static const char *TAG = "ESP_HID_GAP";

extern void ble_hid_task_start_up(void);

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
static bool s_recovery_identity_rotate_pending = false;
static int64_t s_recovery_pairing_window_opened_at_ms = 0;
static int64_t s_first_pairing_window_opened_at_ms = 0;
static uint32_t s_last_conn_param_mode = 0;

typedef enum {
    BLE_HID_CONN_PARAM_MODE_ACTIVE = 1,
    BLE_HID_CONN_PARAM_MODE_LOW_POWER = 2,
} ble_hid_conn_param_mode_t;

typedef struct {
    bool connected;
    uint16_t conn_handle;
} ble_hid_gap_connection_snapshot_t;

static ble_hid_gap_connection_snapshot_t ble_hid_gap_connection_snapshot(void)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    ble_hid_gap_connection_snapshot_t snapshot = {
        .connected = s_ble_gap_connected,
        .conn_handle = s_ble_gap_conn_handle,
    };
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
    return snapshot;
}

static void ble_hid_gap_set_connection_state(bool connected, uint16_t conn_handle)
{
    portENTER_CRITICAL(&s_ble_gap_state_lock);
    s_ble_gap_connected = connected;
    s_ble_gap_conn_handle = conn_handle;
    s_last_conn_param_mode = 0;
    portEXIT_CRITICAL(&s_ble_gap_state_lock);
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
} ble_hid_gap_adv_state_action_t;

#define BLE_HID_GAP_ADV_FLAG_ACTIVE          (1U << 0)
#define BLE_HID_GAP_ADV_FLAG_LOW_POWER       (1U << 1)
#define BLE_HID_GAP_ADV_FLAG_DIRECTED_PENDING (1U << 2)
#define BLE_HID_GAP_ADV_FLAG_KEY_WAKE_ONLY   (1U << 3)
#define BLE_HID_GAP_ADV_FLAG_SHUTDOWN_QUIESCE (1U << 4)
#define BLE_HID_GAP_ADV_FLAG_NIMBLE_READY    (1U << 5)
#define BLE_HID_GAP_ADV_FLAG_HID_STARTED     (1U << 6)
#define BLE_HID_GAP_ADV_FLAG_CONNECTED       (1U << 7)

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
           (conn.connected ? BLE_HID_GAP_ADV_FLAG_CONNECTED : 0U);
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
 * flags + appearance + one 16-bit HID UUID + a short local name. During the
 * first-pairing / recovery window we temporarily trade the primary HID UUID for
 * the Microsoft Swift Pair manufacturer section so Windows can show its native
 * "Connect" toast; the GATT database still exposes HID/audio/OTA services after
 * the central connects.
 */
#define BLE_HID_ADV_NAME_MAX_LEN 17
#define BLE_HID_SCAN_RSP_NAME_MAX_LEN 29
#define BLE_HID_SWIFT_PAIR_MFG_DATA_LEN 5
#define BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITH_APPEARANCE 17
#define BLE_HID_SWIFT_PAIR_DISPLAY_NAME_MAX_WITHOUT_APPEARANCE 21
#define BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE "ble_gap"
#define BLE_HID_GAP_SERVICE_CHANGED_STATE_KEY "svcchg_fw"
#define BLE_HID_GAP_RANDOM_IDENTITY_ADDR_KEY "rnd_id_addr"
#define BLE_HID_GAP_GATT_SCHEMA_REV "ota_v2"
#define BLE_HID_GAP_SERVICE_CHANGED_START_HANDLE 0x0001
#define BLE_HID_GAP_SERVICE_CHANGED_END_HANDLE 0xffff
#define BLE_HID_GAP_RECOVERY_PAIRING_WINDOW_MS 120000LL
#define BLE_HID_GAP_FIRST_PAIRING_WINDOW_MS 60000LL
#define BLE_HID_GAP_SWIFT_PAIR_ADV_MIN_RESTART_MS 1000LL

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

static int64_t ble_hid_gap_now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
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
        s_recovery_pairing_window_active = false;
        ESP_LOGI(TAG, "recovery: pairing window expired");
        return 0;
    }

    return remaining_ms;
}

static bool ble_hid_gap_recovery_pairing_window_open(void)
{
    return ble_hid_gap_recovery_pairing_window_remaining_ms() > 0;
}

static int64_t ble_hid_gap_first_pairing_window_remaining_ms(
    int bonded_peer_count)
{
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

static void ble_hid_gap_open_recovery_pairing_window(void)
{
    s_recovery_pairing_window_active = true;
    s_recovery_pairing_window_opened_at_ms = ble_hid_gap_now_ms();
}

static void ble_hid_gap_close_recovery_pairing_window(const char *reason)
{
    if (!s_recovery_pairing_window_active) {
        return;
    }

    s_recovery_pairing_window_active = false;
    ESP_LOGI(TAG, "recovery: pairing window closed: %s", reason);
}

static void ble_hid_gap_log_identity_addr(const char *context, const uint8_t addr[6])
{
    ESP_LOGI(
        TAG,
        "%s: own_addr_type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
        context,
        s_own_addr_type,
        addr[0],
        addr[1],
        addr[2],
        addr[3],
        addr[4],
        addr[5]);
}

static esp_err_t ble_hid_gap_apply_static_random_identity(
    const uint8_t addr[6],
    const char *context)
{
    int rc = ble_hs_id_set_rnd(addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "%s: BLE static random identity apply failed rc=%d", context, rc);
        return ESP_FAIL;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        s_own_addr_type = BLE_OWN_ADDR_RANDOM;
        ESP_LOGW(TAG, "%s: address type infer failed after random identity rc=%d", context, rc);
    }

    ble_hid_gap_log_identity_addr(context, addr);
    return ESP_OK;
}

static esp_err_t ble_hid_gap_store_static_random_identity(
    const uint8_t addr[6],
    const char *context)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(
            nvs,
            BLE_HID_GAP_RANDOM_IDENTITY_ADDR_KEY,
            addr,
            6);
        if (ret == ESP_OK) {
            ret = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s: BLE static random identity persist failed: %s",
                 context,
                 esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t ble_hid_gap_load_static_random_identity(void)
{
    uint8_t addr[6] = {0};
    size_t addr_len = sizeof(addr);
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        BLE_HID_GAP_SERVICE_CHANGED_NVS_NAMESPACE,
        NVS_READONLY,
        &nvs);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "BLE recovery identity: none stored; using controller identity");
        } else {
            ESP_LOGW(TAG, "BLE recovery identity unavailable: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    ret = nvs_get_blob(
        nvs,
        BLE_HID_GAP_RANDOM_IDENTITY_ADDR_KEY,
        addr,
        &addr_len);
    nvs_close(nvs);

    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "BLE recovery identity: none stored; using controller identity");
        } else {
            ESP_LOGW(TAG, "BLE recovery identity read failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    if (addr_len != sizeof(addr)) {
        ESP_LOGW(TAG, "BLE recovery identity invalid length=%u", (unsigned)addr_len);
        return ESP_ERR_INVALID_SIZE;
    }

    return ble_hid_gap_apply_static_random_identity(addr, "BLE recovery identity loaded");
}

static esp_err_t ble_hid_gap_rotate_static_random_identity(const char *context)
{
    ble_addr_t addr = {0};
    int rc = ble_hs_id_gen_rnd(0, &addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "%s: BLE static random identity generation failed rc=%d",
                 context,
                 rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 5, (uint32_t)rc, 0, s_ble_gap_conn_handle);
        return ESP_FAIL;
    }

    esp_err_t ret = ble_hid_gap_store_static_random_identity(addr.val, context);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 5, (uint32_t)ret, 1, s_ble_gap_conn_handle);
        return ret;
    }

    ret = ble_hid_gap_apply_static_random_identity(addr.val, context);
    if (ret != ESP_OK) {
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 5, (uint32_t)ret, 2, s_ble_gap_conn_handle);
        return ret;
    }

    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             5, 0, 0, s_ble_gap_conn_handle);
    return ESP_OK;
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
    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    if (!conn.connected || conn.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_last_conn_param_mode == (uint32_t)mode) {
        ESP_LOGI(
            TAG,
            "%s connection parameters already requested: conn=%u preferred_itvl=%u-%u latency=%u timeout=%u mode=%u",
            policy,
            conn.conn_handle,
            itvl_min,
            itvl_max,
            latency,
            supervision_timeout,
            (unsigned)mode);
        return ESP_OK;
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
    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
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

    bool name_in_adv = ble_hid_gap_configure_normal_adv_fields();
    device_settings_mark_ble_name_applied();
    ESP_LOGI(TAG,
             "%s: BLE device name refreshed name=%s len=%u changed=%u name_in_adv=%s",
             context,
             s_adv_device_name,
             (unsigned)s_adv_device_name_len,
             changed ? 1u : 0u,
             name_in_adv ? "yes" : "scan_rsp");
    return ESP_OK;
}

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    s_adv_device_name = device_name;
    s_adv_device_name_len = strlen(device_name);
    s_adv_appearance = appearance;
    bool name_in_adv = ble_hid_gap_configure_normal_adv_fields();

    /* Initialize the security configuration */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

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
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_WARN,
                     0, (uint32_t)event->connect.status, event->connect.conn_handle, 0);
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

        ble_hid_gap_set_connection_state(true, event->connect.conn_handle);
        ble_diag_log_on_gap_connect(event->connect.conn_handle);
        status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, true);
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_connect(event->connect.conn_handle);
        }
        s_last_adv_was_directed = false;
        s_service_changed_queued_for_conn = false;
        ble_hid_gap_queue_service_changed("connect");

        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        const bool conn_desc_valid = rc == 0;
        if (rc == 0) {
            ESP_LOGI(
                TAG,
                "security state before initiate: encrypted=%u authenticated=%u bonded=%u key_size=%u",
                desc.sec_state.encrypted,
                desc.sec_state.authenticated,
                desc.sec_state.bonded,
                desc.sec_state.key_size);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CONN_PARAM, DIAG_SEV_INFO,
                     desc.conn_itvl, desc.conn_latency, desc.supervision_timeout, event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "connection descriptor lookup failed before security initiate: rc=%d", rc);
        }

        if (conn_desc_valid) {
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc == 0) {
                ESP_LOGI(TAG, "security initiate requested");
            } else if (rc == BLE_HS_EALREADY) {
                ESP_LOGI(TAG, "security already in progress");
            } else {
                ESP_LOGW(TAG, "security initiate failed: rc=%d", rc);
            }
        } else {
            ESP_LOGW(TAG, "security initiate skipped: missing connection descriptor");
        }

        if (s_audio_enabled) {
            ESP_LOGI(TAG, "audio high-speed link request deferred until active recording");
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_BOND, DIAG_SEV_WARN,
                 0, (uint32_t)event->disconnect.reason, event->disconnect.conn.conn_handle, 0);
        ble_hid_gap_set_connection_state(false, BLE_HS_CONN_HANDLE_NONE);
        status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
        s_service_changed_queued_for_conn = false;
        if (s_audio_enabled) {
            ble_audio_stream_on_gap_disconnect(event->disconnect.conn.conn_handle);
        }
        ble_diag_log_on_gap_disconnect(event->disconnect.conn.conn_handle);
        ble_firmware_ota_on_gap_disconnect(event->disconnect.conn.conn_handle);
        s_directed_adv_pending = true;
        s_last_adv_was_directed = false;
        if (s_shutdown_quiesce) {
            s_directed_adv_pending = false;
            ESP_LOGW(TAG, "shutdown quiesce active: suppressing advertising restart after disconnect");
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_DISCONNECT_SUPPRESS,
                (uint32_t)event->disconnect.reason,
                ble_hid_gap_adv_active_snapshot(),
                event->disconnect.conn.conn_handle,
                DIAG_SEV_WARN);
            return 0;
        }
        ble_hid_gap_keep_recovery_adv_connectable("disconnect");
        if (s_key_wake_only_advertising) {
            s_directed_adv_pending = false;
            ESP_LOGI(TAG, "key-wake-only idle: suppressing advertising restart after disconnect");
            ble_hid_gap_log_adv_state(
                BLE_HID_GAP_ADV_STATE_DISCONNECT_SUPPRESS,
                (uint32_t)event->disconnect.reason,
                ble_hid_gap_adv_active_snapshot(),
                event->disconnect.conn.conn_handle,
                DIAG_SEV_INFO);
            return 0;
        }
        bool recovery_identity_rotated = false;
        if (s_recovery_identity_rotate_pending) {
            esp_err_t rotate_ret =
                ble_hid_gap_rotate_static_random_identity("BLE recovery identity rotated after disconnect");
            s_recovery_identity_rotate_pending = false;
            if (rotate_ret != ESP_OK) {
                ESP_LOGE(TAG,
                         "recovery: identity rotation failed after disconnect; advertising suppressed: %s",
                         esp_err_to_name(rotate_ret));
                s_directed_adv_pending = false;
                status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
                return 0;
            }
            s_directed_adv_pending = false;
            s_last_adv_was_directed = false;
            recovery_identity_rotated = true;
        }
        esp_err_t adv_ret = ble_hid_gap_start_advertising();
        if (recovery_identity_rotated) {
            if (adv_ret != ESP_OK) {
                ESP_LOGE(TAG, "recovery: advertising restart failed after disconnect: %s",
                         esp_err_to_name(adv_ret));
                status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_adv_restart_failed");
                return 0;
            }
            ESP_LOGW(TAG, "recovery: pairing reset complete after disconnect, BLE identity rotated and device is discoverable for first-time pairing");
            status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE);
            status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                     4, 0, 0, s_ble_gap_conn_handle);
        }
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d",
                event->conn_update.status);
        ble_hid_gap_log_conn_desc("connection updated", event->conn_update.conn_handle);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc == 0) {
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_CONN_PARAM, DIAG_SEV_INFO,
                     desc.conn_itvl, desc.conn_latency, desc.supervision_timeout, event->conn_update.conn_handle);
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
        }
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
                    ble_hid_gap_close_recovery_pairing_window("secure connection established");
                }
            } else {
                ESP_LOGW(TAG, "connection descriptor lookup failed after encryption: rc=%d", rc);
            }
            ble_hid_task_start_up();
        } else {
            ESP_LOGW(TAG, "encryption failed or connection already gone; status=%d", event->enc_change.status);
            if (ble_hid_gap_recovery_pairing_window_open() &&
                !s_recovery_identity_rotate_pending) {
                s_recovery_identity_rotate_pending = true;
                ESP_LOGW(
                    TAG,
                    "recovery: stale pairing encryption failure status=%d; identity will rotate after disconnect",
                    event->enc_change.status);
                diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                         7, (uint32_t)event->enc_change.status, 0, event->enc_change.conn_handle);
                int term_rc = ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                if (term_rc != 0) {
                    ESP_LOGW(
                        TAG,
                        "recovery: stale pairing disconnect request failed rc=%d; pending identity rotation kept",
                        term_rc);
                    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                             7, (uint32_t)event->enc_change.status, (uint32_t)term_rc,
                             event->enc_change.conn_handle);
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

    (void)ble_hid_gap_load_static_random_identity();

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

    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    if (conn.connected) {
        ESP_LOGI(TAG, "NimBLE advertising skipped: GAP already connected conn_handle=%u", conn.conn_handle);
        ble_hid_gap_log_adv_state(
            BLE_HID_GAP_ADV_STATE_SKIP_CONNECTED,
            0,
            false,
            conn.conn_handle,
            DIAG_SEV_INFO);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_INFO,
                 2, 0, 1, conn.conn_handle);
        status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, false);
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
    const int64_t first_pairing_remaining_ms = rc == 0
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

    const bool swift_pair_requested = pairing_window || first_pairing_window;
    const bool swift_pair_enabled =
        swift_pair_requested && ble_hid_gap_configure_swift_pair_fields();
    if (!swift_pair_enabled) {
        (void)ble_hid_gap_configure_normal_adv_fields();
    }
    ESP_LOGI(
        TAG,
        "NimBLE advertisement payload profile=%s name_len=%u recovery_window=%u first_pairing_window=%u bonded_peers=%d",
        swift_pair_enabled ? "swift_pair" : "normal",
        (unsigned)s_adv_device_name_len,
        pairing_window ? 1u : 0u,
        first_pairing_window ? 1u : 0u,
        bonded_peer_count);

    rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, swift_pair_enabled ? 7 : 1, 0);
        return ESP_FAIL;
    }

    rc = ble_gap_adv_rsp_set_fields(&s_scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting scan response data; rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_ADV_START, DIAG_SEV_WARN,
                 0, (uint32_t)rc, swift_pair_enabled ? 8 : 2, 0);
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
        recovery_pairing_remaining_ms > 0
            ? recovery_pairing_remaining_ms
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
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
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

esp_err_t ble_hid_gap_mark_stack_ready(void)
{
    s_hid_start_event_seen = true;
    if (!s_nimble_stack_ready) {
        ESP_LOGI(TAG, "HID START received; waiting for NimBLE host sync before advertising");
        return ESP_OK;
    }
    return ble_hid_gap_start_advertising();
}

esp_err_t ble_hid_gap_forget_bonds_and_repair(void)
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
    if (refresh_pairing_window) {
        ESP_LOGW(TAG, "recovery: pairing window already active; rotating identity and refreshing advertising");
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
                 6, 0, 0, s_ble_gap_conn_handle);
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

        esp_err_t rotate_ret =
            ble_hid_gap_rotate_static_random_identity("BLE recovery identity rotated during active pairing window");
        if (rotate_ret != ESP_OK) {
            ESP_LOGE(TAG, "recovery: pairing window identity refresh failed: %s", esp_err_to_name(rotate_ret));
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                     6, (uint32_t)rotate_ret, 1, s_ble_gap_conn_handle);
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
            return rotate_ret;
        }

        esp_err_t adv_ret = ble_hid_gap_start_advertising();
        if (adv_ret != ESP_OK) {
            ESP_LOGE(TAG, "recovery: pairing window advertising refresh failed: %s", esp_err_to_name(adv_ret));
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                     6, (uint32_t)adv_ret, 0, s_ble_gap_conn_handle);
            return adv_ret;
        }

        ESP_LOGW(TAG, "recovery: pairing window refreshed with new BLE identity, device remains discoverable for first-time pairing");
        status_led_notify_ble_repairing("ble_recovery_refresh_pairing");
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "recovery: clearing pairing bonds count=%d", bonded_peer_count);
    status_led_notify_ble_repairing("ble_recovery_clear_bonds");
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
             1, 0, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
    rc = ble_store_clear();
    if (rc != 0) {
        ESP_LOGE(TAG, "recovery: BLE store clear failed rc=%d", rc);
        diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_ERROR,
                 1, (uint32_t)rc, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_store_clear_failed");
        return ESP_FAIL;
    }

    ble_hid_gap_open_recovery_pairing_window();
    s_directed_adv_pending = false;
    s_last_adv_was_directed = false;

    ble_hid_gap_connection_snapshot_t conn = ble_hid_gap_connection_snapshot();
    if (conn.connected && conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        s_recovery_identity_rotate_pending = true;
        rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == 0) {
            ESP_LOGW(TAG, "recovery: active BLE connection terminating for re-pair; identity rotates before advertising restarts");
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     2, 0, (uint32_t)bonded_peer_count, conn.conn_handle);
            return ESP_OK;
        } else {
            s_recovery_identity_rotate_pending = false;
            ESP_LOGW(TAG, "recovery: BLE terminate failed rc=%d", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     2, (uint32_t)rc, (uint32_t)bonded_peer_count, conn.conn_handle);
            return ESP_FAIL;
        }
    } else if (ble_gap_adv_active()) {
        rc = ble_gap_adv_stop();
        if (rc != 0) {
            ESP_LOGW(TAG, "recovery: advertising stop failed rc=%d; restart will continue", rc);
            diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_WARN,
                     3, (uint32_t)rc, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
        }
    }

    esp_err_t rotate_ret =
        ble_hid_gap_rotate_static_random_identity("BLE recovery identity rotated for re-pair");
    if (rotate_ret != ESP_OK) {
        ESP_LOGE(TAG, "recovery: identity rotation failed before advertising restart: %s",
                 esp_err_to_name(rotate_ret));
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_recovery_identity_rotate_failed");
        return rotate_ret;
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

    ESP_LOGW(TAG, "recovery: pairing reset complete, BLE identity rotated and device is discoverable for first-time pairing");
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_BLE);
    status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
    diag_log(DIAG_SRC_BLE_GAP, DIAG_GAP_RECOVERY, DIAG_SEV_INFO,
             4, 0, (uint32_t)bonded_peer_count, s_ble_gap_conn_handle);
    return ESP_OK;
}

bool ble_hid_gap_is_connected(void)
{
    return ble_hid_gap_connection_snapshot().connected;
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
    if (s_ble_gap_connected) {
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
        status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
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
    return ble_hid_gap_request_connection_params(
        "low-power idle",
        BLE_HID_GAP_LOW_POWER_ITVL_MIN,
        BLE_HID_GAP_LOW_POWER_ITVL_MAX,
        BLE_HID_GAP_LOW_POWER_LATENCY,
        BLE_HID_GAP_LOW_POWER_SUPERVISION_TIMEOUT,
        BLE_HID_CONN_PARAM_MODE_LOW_POWER);
}

esp_err_t ble_hid_gap_request_active_connection(void)
{
    esp_err_t params_ret = ble_hid_gap_request_connection_params(
        "active",
        BLE_HID_GAP_ACTIVE_ITVL_MIN,
        BLE_HID_GAP_ACTIVE_ITVL_MAX,
        BLE_HID_GAP_ACTIVE_LATENCY,
        BLE_HID_GAP_ACTIVE_SUPERVISION_TIMEOUT,
        BLE_HID_CONN_PARAM_MODE_ACTIVE);
    (void)ble_hid_gap_request_preferred_2m_phy("active audio");
    return params_ret;
}
