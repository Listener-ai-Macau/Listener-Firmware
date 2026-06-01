#include "ble_diag_log.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_log.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "power_manager.h"

#define BLE_DIAG_LOG_CONTROL_MAX_BYTES 128
#define BLE_DIAG_LOG_CHUNK_HEADER_BYTES 8U
#define BLE_DIAG_LOG_ATT_HEADER_BYTES 3U
#define BLE_DIAG_LOG_EVENT_BYTES DIAG_LOG_EVENT_WIRE_BYTES
#define BLE_DIAG_LOG_MAX_EVENTS_PER_CHUNK 4U

typedef enum {
    BLE_DIAG_LOG_GATT_ATTR_CONTROL = 1,
    BLE_DIAG_LOG_GATT_ATTR_DATA = 2,
    BLE_DIAG_LOG_GATT_ATTR_COUNT = 3,
} ble_diag_log_gatt_attr_t;

/* Notification chunk header: [event_count:2][global_offset:2][crc32:4] */
typedef struct {
    uint16_t event_count;
    uint16_t global_offset;
    uint32_t events_crc;
} diag_log_chunk_header_t;

static const char *TAG = "ble_diag_log";
static const ble_uuid128_t s_service_uuid = BLE_DIAG_LOG_SERVICE_UUID;
static const ble_uuid128_t s_control_uuid = BLE_DIAG_LOG_CONTROL_UUID;
static const ble_uuid128_t s_data_uuid = BLE_DIAG_LOG_DATA_UUID;
static const ble_uuid128_t s_count_uuid = BLE_DIAG_LOG_COUNT_UUID;
static bool s_registered;
static uint16_t s_data_val_handle;

/* Export session state */
static bool s_exporting;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_att_mtu = BLE_ATT_MTU_DFLT;
static uint16_t s_att_value_max_bytes = BLE_ATT_MTU_DFLT - BLE_DIAG_LOG_ATT_HEADER_BYTES;
static uint32_t s_export_offset;

static int ble_diag_log_copy_mbuf(
    struct os_mbuf *om,
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t *out_len)
{
    if (om == NULL || buffer == NULL || out_len == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(om);
    if ((size_t)len > buffer_size) {
        ESP_LOGW(TAG, "GATT write too large: len=%u max=%u", len, (unsigned)buffer_size);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (len > 0 && os_mbuf_copydata(om, 0, len, buffer) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    *out_len = len;
    return 0;
}

static const char *ble_diag_log_find_json_value(const char *json, const char *key)
{
    char pattern[32];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (written <= 0 || written >= (int)sizeof(pattern)) {
        return NULL;
    }

    const char *found = strstr(json, pattern);
    if (found == NULL) {
        return NULL;
    }
    const char *colon = strchr(found + written, ':');
    if (colon == NULL) {
        return NULL;
    }
    const char *value = colon + 1;
    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    return value;
}

static bool ble_diag_log_json_string(
    const char *json,
    const char *key,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    const char *value = ble_diag_log_find_json_value(json, key);
    if (value == NULL || *value != '"') {
        return false;
    }

    value++;
    size_t used = 0;
    while (*value != '\0' && *value != '"') {
        char ch = *value++;
        if (ch == '\\' && *value != '\0') {
            ch = *value++;
        }
        if (used + 1 >= out_size) {
            return false;
        }
        out[used++] = ch;
    }
    if (*value != '"') {
        return false;
    }
    out[used] = '\0';
    return true;
}

static bool ble_diag_log_json_uint32(
    const char *json,
    const char *key,
    uint32_t *out)
{
    if (out == NULL) {
        return false;
    }

    const char *value = ble_diag_log_find_json_value(json, key);
    if (value == NULL || !isdigit((unsigned char)*value)) {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value) {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static uint16_t ble_diag_log_max_event_per_chunk(void)
{
    if (s_att_value_max_bytes <= BLE_DIAG_LOG_CHUNK_HEADER_BYTES) {
        return 0;
    }

    uint16_t payload = (uint16_t)(s_att_value_max_bytes - BLE_DIAG_LOG_CHUNK_HEADER_BYTES);
    uint16_t mtu_events = (uint16_t)(payload / BLE_DIAG_LOG_EVENT_BYTES);
    if (mtu_events > BLE_DIAG_LOG_MAX_EVENTS_PER_CHUNK) {
        return BLE_DIAG_LOG_MAX_EVENTS_PER_CHUNK;
    }
    return mtu_events;
}

static void ble_diag_log_apply_mtu(uint16_t conn_handle, uint16_t mtu, const char *reason)
{
    if (mtu < BLE_ATT_MTU_DFLT) {
        mtu = BLE_ATT_MTU_DFLT;
    }
    if (mtu > BLE_ATT_MTU_MAX) {
        mtu = BLE_ATT_MTU_MAX;
    }

    uint16_t value_max = (uint16_t)(mtu - BLE_DIAG_LOG_ATT_HEADER_BYTES);
    if (value_max > BLE_ATT_ATTR_MAX_LEN) {
        value_max = BLE_ATT_ATTR_MAX_LEN;
    }

    s_att_mtu = mtu;
    s_att_value_max_bytes = value_max;
    ESP_LOGI(
        TAG,
        "diag export MTU updated: reason=%s conn=%u mtu=%u value_max=%u",
        reason != NULL ? reason : "unknown",
        conn_handle,
        s_att_mtu,
        s_att_value_max_bytes);
}

static void ble_diag_log_refresh_current_mtu(uint16_t conn_handle, const char *reason)
{
    uint16_t mtu = ble_att_mtu(conn_handle);
    ble_diag_log_apply_mtu(conn_handle, mtu, reason);
}

static void ble_diag_log_stop_export(void)
{
    if (!s_exporting) {
        return;
    }
    s_exporting = false;
    s_export_offset = 0;
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_DIAG_EXPORT, false);
    ESP_LOGI(TAG, "export session stopped");
}

static int ble_diag_log_send_chunk(uint32_t offset)
{
    if (!s_exporting) {
        return 0;
    }

    ble_diag_log_refresh_current_mtu(s_conn_handle, "read");
    uint16_t max_events = ble_diag_log_max_event_per_chunk();
    if (max_events == 0) {
        ESP_LOGW(
            TAG,
            "diag export MTU too small: conn=%u mtu=%u value_max=%u need_min=%u",
            s_conn_handle,
            s_att_mtu,
            s_att_value_max_bytes,
            (unsigned)(BLE_DIAG_LOG_CHUNK_HEADER_BYTES + BLE_DIAG_LOG_EVENT_BYTES));
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    uint32_t total = diag_log_count();

    uint16_t buf_size = (uint16_t)(max_events * BLE_DIAG_LOG_EVENT_BYTES);
    uint8_t *event_buf = (uint8_t *)malloc(buf_size);
    if (event_buf == NULL) {
        ESP_LOGE(TAG, "failed to allocate event buffer");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    uint32_t read_count = diag_log_read_range(offset, max_events, event_buf, buf_size);
    if (read_count == 0) {
        free(event_buf);
        ESP_LOGI(TAG, "no events at offset %" PRIu32, offset);
        return 0;
    }

    /* Build notification: chunk header + events */
    uint16_t data_len =
        BLE_DIAG_LOG_CHUNK_HEADER_BYTES + (uint16_t)(read_count * BLE_DIAG_LOG_EVENT_BYTES);
    if (data_len > s_att_value_max_bytes) {
        free(event_buf);
        ESP_LOGW(
            TAG,
            "diag export chunk exceeds negotiated ATT value size: len=%u value_max=%u events=%" PRIu32,
            data_len,
            s_att_value_max_bytes,
            read_count);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    uint8_t *notify_buf = (uint8_t *)malloc(data_len);
    if (notify_buf == NULL) {
        free(event_buf);
        ESP_LOGE(TAG, "failed to allocate notify buffer");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    /* Chunk header */
    diag_log_chunk_header_t header = {
        .event_count = (uint16_t)read_count,
        .global_offset = (uint16_t)offset,
        .events_crc = esp_crc32_le(0, event_buf, read_count * BLE_DIAG_LOG_EVENT_BYTES),
    };

    memcpy(notify_buf, &header, BLE_DIAG_LOG_CHUNK_HEADER_BYTES);
    memcpy(
        notify_buf + BLE_DIAG_LOG_CHUNK_HEADER_BYTES,
        event_buf,
        read_count * BLE_DIAG_LOG_EVENT_BYTES);

    free(event_buf);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(notify_buf, data_len);
    free(notify_buf);

    if (om == NULL) {
        ESP_LOGE(TAG, "failed to create mbuf for notification");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_data_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: rc=%d offset=%" PRIu32, rc, offset);
        return BLE_ATT_ERR_UNLIKELY;
    } else {
        s_export_offset = offset + read_count;
        ESP_LOGI(
            TAG,
            "sent chunk offset=%" PRIu32 " count=%" PRIu32 " total=%" PRIu32 " crc=0x%08lx value_len=%u value_max=%u",
            offset,
            read_count,
            total,
            (unsigned long)header.events_crc,
            data_len,
            s_att_value_max_bytes);
    }
    return 0;
}

static int ble_diag_log_handle_control_write(uint16_t conn_handle, struct os_mbuf *om)
{
    uint8_t raw[BLE_DIAG_LOG_CONTROL_MAX_BYTES + 1];
    uint16_t len = 0;
    int att_err = ble_diag_log_copy_mbuf(om, raw, BLE_DIAG_LOG_CONTROL_MAX_BYTES, &len);
    if (att_err != 0) {
        return att_err;
    }
    raw[len] = '\0';

    char op[16];
    if (!ble_diag_log_json_string((const char *)raw, "op", op, sizeof(op))) {
        ESP_LOGW(TAG, "control missing op");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (strcmp(op, "start") == 0) {
        if (s_exporting) {
            ESP_LOGW(TAG, "export already in progress");
            return 0;
        }
        s_conn_handle = conn_handle;
        ble_diag_log_refresh_current_mtu(conn_handle, "start");
        if (ble_diag_log_max_event_per_chunk() == 0) {
            ESP_LOGW(
                TAG,
                "export start rejected: MTU too small conn=%u mtu=%u value_max=%u need_min=%u",
                conn_handle,
                s_att_mtu,
                s_att_value_max_bytes,
                (unsigned)(BLE_DIAG_LOG_CHUNK_HEADER_BYTES + BLE_DIAG_LOG_EVENT_BYTES));
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        s_exporting = true;
        s_export_offset = 0;
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_DIAG_EXPORT, true);
        ESP_LOGI(
            TAG,
            "export session started, conn=%u total=%" PRIu32 " mtu=%u value_max=%u events_per_chunk=%u",
            conn_handle,
            diag_log_count(),
            s_att_mtu,
            s_att_value_max_bytes,
            ble_diag_log_max_event_per_chunk());
        return 0;
    }

    if (strcmp(op, "read") == 0) {
        if (!s_exporting) {
            ESP_LOGW(TAG, "read without active export session");
            return BLE_ATT_ERR_UNLIKELY;
        }
        uint32_t offset = 0;
        if (!ble_diag_log_json_uint32((const char *)raw, "offset", &offset)) {
            ESP_LOGW(TAG, "read missing offset");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        return ble_diag_log_send_chunk(offset);
    }

    if (strcmp(op, "stop") == 0) {
        ble_diag_log_stop_export();
        return 0;
    }

    ESP_LOGW(TAG, "unknown op=%s", op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_diag_log_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;

    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ble_diag_log_gatt_attr_t attr = (ble_diag_log_gatt_attr_t)(uintptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr == BLE_DIAG_LOG_GATT_ATTR_COUNT) {
            char resp[32];
            int n = snprintf(resp, sizeof(resp), "{\"count\":%" PRIu32 "}", diag_log_count());
            if (n <= 0 || n >= (int)sizeof(resp)) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            int rc = os_mbuf_append(ctxt->om, resp, (size_t)n);
            return rc != 0 ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr == BLE_DIAG_LOG_GATT_ATTR_CONTROL) {
            s_conn_handle = conn_handle;
            return ble_diag_log_handle_control_write(conn_handle, ctxt->om);
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_diag_log_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_control_uuid.u,
                .access_cb = ble_diag_log_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_DIAG_LOG_GATT_ATTR_CONTROL,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = ble_diag_log_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_data_val_handle,
                .arg = (void *)(uintptr_t)BLE_DIAG_LOG_GATT_ATTR_DATA,
            },
            {
                .uuid = &s_count_uuid.u,
                .access_cb = ble_diag_log_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_DIAG_LOG_GATT_ATTR_COUNT,
            },
            {0},
        },
    },
    {0},
};

int ble_diag_log_register_gatt(void)
{
    if (s_registered) {
        return 0;
    }

    int rc = ble_gatts_count_cfg(s_diag_log_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return -1;
    }

    rc = ble_gatts_add_svcs(s_diag_log_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return -1;
    }

    s_registered = true;
    ESP_LOGI(TAG, "diagnostic log GATT service registered");
    return 0;
}

void ble_diag_log_on_gap_connect(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
    s_export_offset = 0;
    ble_diag_log_refresh_current_mtu(conn_handle, "connect");
}

void ble_diag_log_on_gap_disconnect(uint16_t conn_handle)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && conn_handle != s_conn_handle) {
        ESP_LOGD(TAG, "stale disconnect ignored: conn=%u active=%u", conn_handle, s_conn_handle);
        return;
    }

    if (s_exporting) {
        ESP_LOGW(TAG, "BLE disconnect during export, aborting");
        ble_diag_log_stop_export();
    }
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_att_mtu = BLE_ATT_MTU_DFLT;
    s_att_value_max_bytes = BLE_ATT_MTU_DFLT - BLE_DIAG_LOG_ATT_HEADER_BYTES;
}

void ble_diag_log_on_gap_mtu(uint16_t conn_handle, uint16_t mtu)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        s_conn_handle = conn_handle;
    }

    if (conn_handle != s_conn_handle) {
        ESP_LOGD(TAG, "stale MTU ignored: conn=%u active=%u mtu=%u", conn_handle, s_conn_handle, mtu);
        return;
    }

    ble_diag_log_apply_mtu(conn_handle, mtu, "gap_mtu");
}

void ble_diag_log_log_gatt_state(void)
{
    uint16_t service_handle = 0;
    uint16_t control_def_handle = 0;
    uint16_t control_val_handle = 0;
    uint16_t data_def_handle = 0;
    uint16_t count_def_handle = 0;
    uint16_t count_val_handle = 0;
    int svc_rc = ble_gatts_find_svc(&s_service_uuid.u, &service_handle);
    int control_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_control_uuid.u,
        &control_def_handle,
        &control_val_handle);
    int data_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_data_uuid.u,
        &data_def_handle,
        &s_data_val_handle);
    int count_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_count_uuid.u,
        &count_def_handle,
        &count_val_handle);

    ESP_LOGI(
        TAG,
        "diag log GATT state: registered=%u svc_rc=%d svc=%u control_rc=%d ctrl_def=%u ctrl_val=%u data_rc=%d data_def=%u data_val=%u count_rc=%d count_def=%u count_val=%u",
        s_registered,
        svc_rc,
        service_handle,
        control_rc,
        control_def_handle,
        control_val_handle,
        data_rc,
        data_def_handle,
        s_data_val_handle,
        count_rc,
        count_def_handle,
        count_val_handle);
}
