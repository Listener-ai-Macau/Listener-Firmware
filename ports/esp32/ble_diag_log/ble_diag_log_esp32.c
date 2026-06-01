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
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "power_manager.h"

#define BLE_DIAG_LOG_CONTROL_MAX_BYTES 128
#define BLE_DIAG_LOG_CHUNK_HEADER_BYTES 8
#define BLE_DIAG_LOG_DEFAULT_MTU 244

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
static uint16_t s_conn_handle;
static uint16_t s_mtu;
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
    uint16_t payload = s_mtu > BLE_DIAG_LOG_CHUNK_HEADER_BYTES
        ? s_mtu - BLE_DIAG_LOG_CHUNK_HEADER_BYTES
        : BLE_DIAG_LOG_DEFAULT_MTU - BLE_DIAG_LOG_CHUNK_HEADER_BYTES;
    return payload / 24; /* sizeof(diag_event_t) is 24 */
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

static void ble_diag_log_send_chunk(uint32_t offset)
{
    if (!s_exporting) {
        return;
    }

    uint16_t max_events = ble_diag_log_max_event_per_chunk();
    uint32_t total = diag_log_count();

    /* Allocate buffer for events: max_events * 24 bytes */
    uint16_t buf_size = max_events * 24;
    uint8_t *event_buf = (uint8_t *)malloc(buf_size);
    if (event_buf == NULL) {
        ESP_LOGE(TAG, "failed to allocate event buffer");
        return;
    }

    uint32_t read_count = diag_log_read_range(offset, max_events, event_buf, buf_size);
    if (read_count == 0) {
        free(event_buf);
        ESP_LOGI(TAG, "no events at offset %" PRIu32, offset);
        return;
    }

    /* Build notification: chunk header + events */
    uint16_t data_len = BLE_DIAG_LOG_CHUNK_HEADER_BYTES + (uint16_t)(read_count * 24);
    uint8_t *notify_buf = (uint8_t *)malloc(data_len);
    if (notify_buf == NULL) {
        free(event_buf);
        ESP_LOGE(TAG, "failed to allocate notify buffer");
        return;
    }

    /* Chunk header */
    diag_log_chunk_header_t header = {
        .event_count = (uint16_t)read_count,
        .global_offset = (uint16_t)offset,
        .events_crc = esp_crc32_le(0, event_buf, read_count * 24),
    };

    memcpy(notify_buf, &header, BLE_DIAG_LOG_CHUNK_HEADER_BYTES);
    memcpy(notify_buf + BLE_DIAG_LOG_CHUNK_HEADER_BYTES, event_buf, read_count * 24);

    free(event_buf);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(notify_buf, data_len);
    free(notify_buf);

    if (om == NULL) {
        ESP_LOGE(TAG, "failed to create mbuf for notification");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_data_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: rc=%d offset=%" PRIu32, rc, offset);
    } else {
        s_export_offset = offset + read_count;
        ESP_LOGI(TAG, "sent chunk offset=%" PRIu32 " count=%" PRIu32 " total=%" PRIu32 " crc=0x%08lx",
                 offset, read_count, total, (unsigned long)header.events_crc);
    }
}

static int ble_diag_log_handle_control_write(struct os_mbuf *om)
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
        s_exporting = true;
        s_export_offset = 0;
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_DIAG_EXPORT, true);
        ESP_LOGI(TAG, "export session started, total=%" PRIu32, diag_log_count());
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
        ble_diag_log_send_chunk(offset);
        return 0;
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
            return ble_diag_log_handle_control_write(ctxt->om);
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

void ble_diag_log_on_gap_disconnect(uint16_t conn_handle)
{
    (void)conn_handle;
    if (s_exporting) {
        ESP_LOGW(TAG, "BLE disconnect during export, aborting");
        ble_diag_log_stop_export();
    }
}

void ble_diag_log_on_gap_mtu(uint16_t conn_handle, uint16_t mtu)
{
    if (conn_handle == s_conn_handle) {
        s_mtu = mtu;
        ESP_LOGI(TAG, "MTU updated: %u", (unsigned)mtu);
    }
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
