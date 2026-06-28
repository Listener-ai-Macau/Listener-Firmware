#include "ble_firmware_ota.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_log.h"
#include "esp_log.h"
#include "firmware_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "listener_device.h"
#include "os/os_mbuf.h"

#define BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES 384
#define BLE_FIRMWARE_OTA_DATA_MAX_BYTES 512
#define BLE_FIRMWARE_OTA_REBOOT_DELAY_MS 500
#define BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES 2048
#define BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES "firmware_ota_v1;firmware_ota_v2"
#define BLE_FIRMWARE_OTA_V2_CONTROL_SIZE 16
#define BLE_FIRMWARE_OTA_V2_DATA_HEADER_SIZE 4
#define BLE_FIRMWARE_OTA_V2_DATA_PAYLOAD_MAX 500
#define BLE_FIRMWARE_OTA_V2_STATUS_SIZE 24
#define BLE_FIRMWARE_OTA_V2_DEFAULT_WINDOW_CHUNKS 8
#define BLE_FIRMWARE_OTA_V2_MAGIC0 ('L')
#define BLE_FIRMWARE_OTA_V2_MAGIC1 ('O')
#define BLE_FIRMWARE_OTA_V2_MAGIC2 ('V')
#define BLE_FIRMWARE_OTA_V2_MAGIC3 ('2')
#define BLE_FIRMWARE_OTA_V2_PROTOCOL_VERSION 1
#define BLE_FIRMWARE_OTA_V2_OP_BEGIN 1
#define BLE_FIRMWARE_OTA_V2_OP_SYNC 2
#define BLE_FIRMWARE_OTA_V2_OP_FINISH 3
#define BLE_FIRMWARE_OTA_V2_OP_ABORT 4

typedef enum {
    BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL = 1,
    BLE_FIRMWARE_OTA_GATT_ATTR_DATA = 2,
    BLE_FIRMWARE_OTA_GATT_ATTR_READINESS = 3,
    BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES = 4,
    BLE_FIRMWARE_OTA_GATT_ATTR_V2_CONTROL = 5,
    BLE_FIRMWARE_OTA_GATT_ATTR_V2_DATA = 6,
    BLE_FIRMWARE_OTA_GATT_ATTR_V2_STATUS = 7,
} ble_firmware_ota_gatt_attr_t;

typedef enum {
    BLE_FIRMWARE_OTA_V2_STATE_IDLE = 0,
    BLE_FIRMWARE_OTA_V2_STATE_RECEIVING = 2,
    BLE_FIRMWARE_OTA_V2_STATE_COMPLETE = 3,
    BLE_FIRMWARE_OTA_V2_STATE_ERROR = 4,
} ble_firmware_ota_v2_state_t;

typedef enum {
    BLE_FIRMWARE_OTA_V2_ERROR_NONE = 0,
    BLE_FIRMWARE_OTA_V2_ERROR_BAD_MAGIC = 1,
    BLE_FIRMWARE_OTA_V2_ERROR_BAD_STATE = 2,
    BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE = 3,
    BLE_FIRMWARE_OTA_V2_ERROR_OFFSET_MISMATCH = 4,
    BLE_FIRMWARE_OTA_V2_ERROR_FLASH_WRITE = 5,
    BLE_FIRMWARE_OTA_V2_ERROR_BEGIN_FAILED = 6,
    BLE_FIRMWARE_OTA_V2_ERROR_FINISH_FAILED = 7,
} ble_firmware_ota_v2_error_t;

typedef struct {
    ble_firmware_ota_v2_state_t state;
    ble_firmware_ota_v2_error_t last_error;
    size_t expected_size;
    size_t bytes_written;
    uint32_t data_write_count;
    uint16_t chunk_payload_bytes;
    uint16_t window_chunks;
} ble_firmware_ota_v2_context_t;

static const char *TAG = "ble_firmware_ota";
static const ble_uuid128_t s_service_uuid = BLE_FIRMWARE_OTA_SERVICE_UUID;
static const ble_uuid128_t s_control_uuid = BLE_FIRMWARE_OTA_CONTROL_UUID;
static const ble_uuid128_t s_data_uuid = BLE_FIRMWARE_OTA_DATA_UUID;
static const ble_uuid128_t s_readiness_uuid = BLE_FIRMWARE_OTA_READINESS_UUID;
static const ble_uuid128_t s_capabilities_uuid = BLE_FIRMWARE_OTA_CAPABILITIES_UUID;
static const ble_uuid128_t s_v2_control_uuid = BLE_FIRMWARE_OTA_V2_CONTROL_UUID;
static const ble_uuid128_t s_v2_data_uuid = BLE_FIRMWARE_OTA_V2_DATA_UUID;
static const ble_uuid128_t s_v2_status_uuid = BLE_FIRMWARE_OTA_V2_STATUS_UUID;
static ble_firmware_ota_v2_context_t s_v2;
static bool s_registered;

static void ble_firmware_ota_v2_sync_status(void);
static int ble_firmware_ota_v2_handle_begin(const uint8_t *bytes, uint16_t length);
static int ble_firmware_ota_v2_handle_finish(void);
static int ble_firmware_ota_v2_handle_control_write(struct os_mbuf *om);
static int ble_firmware_ota_v2_handle_data_write(struct os_mbuf *om);

static int ble_firmware_ota_att_error_from_esp(esp_err_t ret)
{
    switch (ret) {
    case ESP_OK:
        return 0;
    case ESP_ERR_NO_MEM:
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int ble_firmware_ota_copy_mbuf(
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

static uint32_t ble_firmware_ota_v2_read_u32_le(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0]) |
           (((uint32_t)bytes[1]) << 8) |
           (((uint32_t)bytes[2]) << 16) |
           (((uint32_t)bytes[3]) << 24);
}

static uint16_t ble_firmware_ota_v2_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0]) | (((uint16_t)bytes[1]) << 8));
}

static void ble_firmware_ota_v2_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24) & 0xffU);
}

static void ble_firmware_ota_v2_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8) & 0xffU);
}

static bool ble_firmware_ota_v2_has_magic(const uint8_t *bytes, uint16_t length)
{
    return length >= 6 &&
           bytes[0] == BLE_FIRMWARE_OTA_V2_MAGIC0 &&
           bytes[1] == BLE_FIRMWARE_OTA_V2_MAGIC1 &&
           bytes[2] == BLE_FIRMWARE_OTA_V2_MAGIC2 &&
           bytes[3] == BLE_FIRMWARE_OTA_V2_MAGIC3 &&
           bytes[5] == BLE_FIRMWARE_OTA_V2_PROTOCOL_VERSION;
}

static void ble_firmware_ota_v2_reset(void)
{
    memset(&s_v2, 0, sizeof(s_v2));
    s_v2.state = BLE_FIRMWARE_OTA_V2_STATE_IDLE;
    s_v2.last_error = BLE_FIRMWARE_OTA_V2_ERROR_NONE;
    s_v2.chunk_payload_bytes = BLE_FIRMWARE_OTA_V2_DATA_PAYLOAD_MAX;
    s_v2.window_chunks = BLE_FIRMWARE_OTA_V2_DEFAULT_WINDOW_CHUNKS;
}

static void ble_firmware_ota_v2_set_error(ble_firmware_ota_v2_error_t error)
{
    s_v2.state = BLE_FIRMWARE_OTA_V2_STATE_ERROR;
    s_v2.last_error = error;
}

static void ble_firmware_ota_v2_set_recoverable_error(ble_firmware_ota_v2_error_t error)
{
    s_v2.last_error = error;
}

static void ble_firmware_ota_v2_sync_status(void)
{
    firmware_ota_status_t status = firmware_ota_get_status();
    if (s_v2.state == BLE_FIRMWARE_OTA_V2_STATE_RECEIVING && status.active) {
        s_v2.bytes_written = status.bytes_written;
        s_v2.expected_size = status.expected_size;
    }
}

static int ble_firmware_ota_v2_append_status(struct os_mbuf *om)
{
    uint8_t status[BLE_FIRMWARE_OTA_V2_STATUS_SIZE] = {0};
    ble_firmware_ota_v2_sync_status();
    status[0] = BLE_FIRMWARE_OTA_V2_MAGIC0;
    status[1] = BLE_FIRMWARE_OTA_V2_MAGIC1;
    status[2] = BLE_FIRMWARE_OTA_V2_MAGIC2;
    status[3] = BLE_FIRMWARE_OTA_V2_MAGIC3;
    status[4] = BLE_FIRMWARE_OTA_V2_PROTOCOL_VERSION;
    status[5] = (uint8_t)s_v2.state;
    status[6] = (uint8_t)s_v2.last_error;
    ble_firmware_ota_v2_write_u32_le(&status[8], (uint32_t)s_v2.bytes_written);
    ble_firmware_ota_v2_write_u32_le(&status[12], (uint32_t)s_v2.expected_size);
    ble_firmware_ota_v2_write_u16_le(&status[16], s_v2.chunk_payload_bytes);
    ble_firmware_ota_v2_write_u16_le(&status[18], s_v2.window_chunks);
    ble_firmware_ota_v2_write_u32_le(&status[20], s_v2.data_write_count);
    return os_mbuf_append(om, status, sizeof(status)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const char *ble_firmware_ota_compact_read_value_if_needed(
    uint16_t conn_handle,
    ble_firmware_ota_gatt_attr_t attr,
    const char *value)
{
    if (value == NULL) {
        return "";
    }
    if (attr != BLE_FIRMWARE_OTA_GATT_ATTR_DATA &&
        attr != BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES) {
        return value;
    }

    uint16_t mtu = ble_att_mtu(conn_handle);
    uint16_t value_max = mtu > 1 ? (uint16_t)(mtu - 1U) : 0U;
    if (mtu <= BLE_ATT_MTU_DFLT && strlen(value) > value_max) {
        ESP_LOGI(
            TAG,
            "OTA identity compact read attr=%u mtu=%u full_len=%u value=%s",
            (unsigned)attr,
            mtu,
            (unsigned)strlen(value),
            BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES);
        return BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES;
    }
    return value;
}

static const char *ble_firmware_ota_find_json_value(const char *json, const char *key)
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

static bool ble_firmware_ota_json_string(
    const char *json,
    const char *key,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    const char *value = ble_firmware_ota_find_json_value(json, key);
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

static bool ble_firmware_ota_json_size(const char *json, const char *key, size_t *out)
{
    if (out == NULL) {
        return false;
    }

    const char *value = ble_firmware_ota_find_json_value(json, key);
    if (value == NULL || !isdigit((unsigned char)*value)) {
        return false;
    }

    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || parsed > SIZE_MAX) {
        return false;
    }

    *out = (size_t)parsed;
    return true;
}

static void ble_firmware_ota_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DELAY_MS));
    firmware_ota_reboot_to_pending_image();
    vTaskDelete(NULL);
}

static esp_err_t ble_firmware_ota_schedule_reboot(void)
{
    BaseType_t started = xTaskCreate(
        ble_firmware_ota_reboot_task,
        "ble_ota_reboot",
        BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL);
    return started == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static int ble_firmware_ota_handle_begin(const char *json)
{
    char version[32];
    size_t image_size = 0;
    if (!ble_firmware_ota_json_string(json, "version", version, sizeof(version)) ||
        !ble_firmware_ota_json_size(json, "size", &image_size)) {
        ESP_LOGW(TAG, "OTA begin control missing version or size");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    esp_err_t ret = firmware_ota_begin(image_size, version);
    ESP_LOGI(TAG, "OTA control begin size=%u version=%s ret=%s",
             (unsigned)image_size, version, esp_err_to_name(ret));
    return ble_firmware_ota_att_error_from_esp(ret);
}

static int ble_firmware_ota_handle_finish(const char *json)
{
    size_t expected_size = 0;
    if (!ble_firmware_ota_json_size(json, "size", &expected_size)) {
        ESP_LOGW(TAG, "OTA finish control missing size");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    firmware_ota_status_t status = firmware_ota_get_status();
    if (status.active && status.expected_size > 0 && status.expected_size != expected_size) {
        ESP_LOGW(
            TAG,
            "OTA finish size mismatch before verify: control=%u expected=%u",
            (unsigned)expected_size,
            (unsigned)status.expected_size);
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    esp_err_t ret = firmware_ota_finish(false);
    ESP_LOGI(TAG, "OTA control finish size=%u ret=%s", (unsigned)expected_size, esp_err_to_name(ret));
    if (ret != ESP_OK) {
        return ble_firmware_ota_att_error_from_esp(ret);
    }

    ret = ble_firmware_ota_schedule_reboot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA reboot scheduling failed: %s", esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        return ble_firmware_ota_att_error_from_esp(ret);
    }
    return 0;
}

static int ble_firmware_ota_v2_handle_begin_json(const char *json)
{
    size_t expected_size = 0;
    size_t chunk_payload_bytes = 0;
    size_t window_chunks = 0;
    if (!ble_firmware_ota_json_size(json, "size", &expected_size) ||
        !ble_firmware_ota_json_size(json, "chunk", &chunk_payload_bytes)) {
        ESP_LOGW(TAG, "OTA v2 JSON begin missing size or chunk");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (!ble_firmware_ota_json_size(json, "window", &window_chunks)) {
        window_chunks = BLE_FIRMWARE_OTA_V2_DEFAULT_WINDOW_CHUNKS;
    }
    if (chunk_payload_bytes > UINT16_MAX || window_chunks > UINT16_MAX) {
        ESP_LOGW(TAG, "OTA v2 JSON begin chunk/window too large");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t raw[BLE_FIRMWARE_OTA_V2_CONTROL_SIZE] = {
        BLE_FIRMWARE_OTA_V2_MAGIC0,
        BLE_FIRMWARE_OTA_V2_MAGIC1,
        BLE_FIRMWARE_OTA_V2_MAGIC2,
        BLE_FIRMWARE_OTA_V2_MAGIC3,
        BLE_FIRMWARE_OTA_V2_OP_BEGIN,
        BLE_FIRMWARE_OTA_V2_PROTOCOL_VERSION,
    };
    ble_firmware_ota_v2_write_u32_le(&raw[8], (uint32_t)expected_size);
    ble_firmware_ota_v2_write_u16_le(&raw[12], (uint16_t)chunk_payload_bytes);
    ble_firmware_ota_v2_write_u16_le(&raw[14], (uint16_t)window_chunks);
    return ble_firmware_ota_v2_handle_begin(raw, sizeof(raw));
}

static int ble_firmware_ota_handle_control_json(const char *json)
{
    char op[16];
    if (!ble_firmware_ota_json_string(json, "op", op, sizeof(op))) {
        ESP_LOGW(TAG, "OTA control missing op");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (strcmp(op, "begin") == 0) {
        return ble_firmware_ota_handle_begin(json);
    }
    if (strcmp(op, "finish") == 0) {
        return ble_firmware_ota_handle_finish(json);
    }
    if (strcmp(op, "abort") == 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        ESP_LOGW(TAG, "OTA control abort requested");
        return 0;
    }
    if (strcmp(op, "begin_v2") == 0) {
        return ble_firmware_ota_v2_handle_begin_json(json);
    }
    if (strcmp(op, "sync_v2") == 0) {
        ble_firmware_ota_v2_sync_status();
        return 0;
    }
    if (strcmp(op, "finish_v2") == 0) {
        return ble_firmware_ota_v2_handle_finish();
    }
    if (strcmp(op, "abort_v2") == 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        ble_firmware_ota_v2_reset();
        ESP_LOGW(TAG, "OTA v2 JSON control abort requested");
        return 0;
    }

    ESP_LOGW(TAG, "OTA control unknown op=%s", op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_firmware_ota_handle_control_write(struct os_mbuf *om)
{
    uint8_t raw[BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES + 1];
    uint16_t len = 0;
    int att_err = ble_firmware_ota_copy_mbuf(om, raw, BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES, &len);
    if (att_err != 0) {
        return att_err;
    }
    raw[len] = '\0';
    return ble_firmware_ota_handle_control_json((const char *)raw);
}

static bool ble_firmware_ota_v2_control_mbuf_has_magic(struct os_mbuf *om)
{
    uint8_t raw[BLE_FIRMWARE_OTA_V2_CONTROL_SIZE];
    uint16_t len = 0;
    return ble_firmware_ota_copy_mbuf(om, raw, sizeof(raw), &len) == 0 &&
           ble_firmware_ota_v2_has_magic(raw, len);
}

static int ble_firmware_ota_handle_data_write(struct os_mbuf *om)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buffer[BLE_FIRMWARE_OTA_DATA_MAX_BYTES + 1];
    int att_err = ble_firmware_ota_copy_mbuf(om, buffer, sizeof(buffer) - 1, &len);
    if (att_err != 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return att_err;
    }
    buffer[len] = '\0';

    if (len <= BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES &&
        buffer[0] == '{' &&
        strstr((const char *)buffer, "\"op\"") != NULL) {
        return ble_firmware_ota_handle_control_json((const char *)buffer);
    }
    if (s_v2.state == BLE_FIRMWARE_OTA_V2_STATE_RECEIVING) {
        return ble_firmware_ota_v2_handle_data_write(om);
    }

    esp_err_t ret = firmware_ota_write(buffer, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA data write failed len=%u ret=%s", len, esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return ble_firmware_ota_att_error_from_esp(ret);
    }
    return 0;
}

static int ble_firmware_ota_v2_handle_begin(const uint8_t *bytes, uint16_t length)
{
    if (length < BLE_FIRMWARE_OTA_V2_CONTROL_SIZE) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    size_t expected_size = ble_firmware_ota_v2_read_u32_le(&bytes[8]);
    uint16_t chunk_payload_bytes = ble_firmware_ota_v2_read_u16_le(&bytes[12]);
    uint16_t window_chunks = ble_firmware_ota_v2_read_u16_le(&bytes[14]);
    if (expected_size == 0 ||
        chunk_payload_bytes == 0 ||
        chunk_payload_bytes > BLE_FIRMWARE_OTA_V2_DATA_PAYLOAD_MAX) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (window_chunks == 0) {
        window_chunks = BLE_FIRMWARE_OTA_V2_DEFAULT_WINDOW_CHUNKS;
    }

    ble_firmware_ota_v2_reset();
    s_v2.expected_size = expected_size;
    s_v2.chunk_payload_bytes = chunk_payload_bytes;
    s_v2.window_chunks = window_chunks;

    esp_err_t ret = firmware_ota_begin(expected_size, "ble_ota_v2");
    ESP_LOGI(TAG,
             "OTA v2 control begin size=%u chunk=%u window=%u ret=%s",
             (unsigned)expected_size,
             (unsigned)chunk_payload_bytes,
             (unsigned)window_chunks,
             esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BEGIN_FAILED);
        return ble_firmware_ota_att_error_from_esp(ret);
    }

    s_v2.state = BLE_FIRMWARE_OTA_V2_STATE_RECEIVING;
    s_v2.last_error = BLE_FIRMWARE_OTA_V2_ERROR_NONE;
    return 0;
}

static int ble_firmware_ota_v2_handle_finish(void)
{
    ble_firmware_ota_v2_sync_status();
    if (s_v2.state != BLE_FIRMWARE_OTA_V2_STATE_RECEIVING) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_STATE);
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (s_v2.expected_size == 0 || s_v2.bytes_written != s_v2.expected_size) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    esp_err_t ret = firmware_ota_finish(false);
    ESP_LOGI(TAG, "OTA v2 control finish size=%u ret=%s", (unsigned)s_v2.expected_size, esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_FINISH_FAILED);
        return ble_firmware_ota_att_error_from_esp(ret);
    }

    s_v2.state = BLE_FIRMWARE_OTA_V2_STATE_COMPLETE;
    s_v2.last_error = BLE_FIRMWARE_OTA_V2_ERROR_NONE;
    ret = ble_firmware_ota_schedule_reboot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA v2 reboot scheduling failed: %s", esp_err_to_name(ret));
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_FINISH_FAILED);
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        return ble_firmware_ota_att_error_from_esp(ret);
    }
    return 0;
}

static int ble_firmware_ota_v2_handle_control_write(struct os_mbuf *om)
{
    uint8_t raw[BLE_FIRMWARE_OTA_V2_CONTROL_SIZE];
    uint16_t len = 0;
    int att_err = ble_firmware_ota_copy_mbuf(om, raw, sizeof(raw), &len);
    if (att_err != 0) {
        return att_err;
    }
    if (!ble_firmware_ota_v2_has_magic(raw, len)) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_MAGIC);
        ESP_LOGW(TAG, "OTA v2 control bad magic/version len=%u", len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    switch (raw[4]) {
    case BLE_FIRMWARE_OTA_V2_OP_BEGIN:
        return ble_firmware_ota_v2_handle_begin(raw, len);
    case BLE_FIRMWARE_OTA_V2_OP_SYNC:
        ble_firmware_ota_v2_sync_status();
        return 0;
    case BLE_FIRMWARE_OTA_V2_OP_FINISH:
        return ble_firmware_ota_v2_handle_finish();
    case BLE_FIRMWARE_OTA_V2_OP_ABORT:
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        ble_firmware_ota_v2_reset();
        ESP_LOGW(TAG, "OTA v2 control abort requested");
        return 0;
    default:
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_MAGIC);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int ble_firmware_ota_v2_handle_data_write(struct os_mbuf *om)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len <= BLE_FIRMWARE_OTA_V2_DATA_HEADER_SIZE) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (len > BLE_FIRMWARE_OTA_DATA_MAX_BYTES) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buffer[BLE_FIRMWARE_OTA_DATA_MAX_BYTES];
    int att_err = ble_firmware_ota_copy_mbuf(om, buffer, sizeof(buffer), &len);
    if (att_err != 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_FLASH_WRITE);
        return att_err;
    }
    if (s_v2.state != BLE_FIRMWARE_OTA_V2_STATE_RECEIVING) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_STATE);
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint32_t offset = ble_firmware_ota_v2_read_u32_le(buffer);
    uint16_t payload_len = (uint16_t)(len - BLE_FIRMWARE_OTA_V2_DATA_HEADER_SIZE);
    if (payload_len == 0 ||
        payload_len > s_v2.chunk_payload_bytes ||
        ((size_t)offset + payload_len) > s_v2.expected_size) {
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_BAD_SIZE);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    ble_firmware_ota_v2_sync_status();
    if (offset < s_v2.bytes_written) {
        if (((size_t)offset + payload_len) <= s_v2.bytes_written) {
            s_v2.data_write_count++;
            return 0;
        }
        ble_firmware_ota_v2_set_recoverable_error(BLE_FIRMWARE_OTA_V2_ERROR_OFFSET_MISMATCH);
        return 0;
    }
    if (offset != s_v2.bytes_written) {
        ble_firmware_ota_v2_set_recoverable_error(BLE_FIRMWARE_OTA_V2_ERROR_OFFSET_MISMATCH);
        return 0;
    }

    esp_err_t ret = firmware_ota_write(&buffer[BLE_FIRMWARE_OTA_V2_DATA_HEADER_SIZE], payload_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA v2 data write failed offset=%u len=%u ret=%s",
                 (unsigned)offset,
                 (unsigned)payload_len,
                 esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        ble_firmware_ota_v2_set_error(BLE_FIRMWARE_OTA_V2_ERROR_FLASH_WRITE);
        return ble_firmware_ota_att_error_from_esp(ret);
    }

    s_v2.bytes_written += payload_len;
    s_v2.data_write_count++;
    s_v2.last_error = BLE_FIRMWARE_OTA_V2_ERROR_NONE;
    return 0;
}

static int ble_firmware_ota_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)attr_handle;

    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ble_firmware_ota_gatt_attr_t attr = (ble_firmware_ota_gatt_attr_t)(uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *value = NULL;
        switch (attr) {
        case BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL:
            if (s_v2.state != BLE_FIRMWARE_OTA_V2_STATE_IDLE) {
                return ble_firmware_ota_v2_append_status(ctxt->om);
            }
            value = listener_device_get_factory_readiness();
            break;
        case BLE_FIRMWARE_OTA_GATT_ATTR_READINESS:
            value = listener_device_get_factory_readiness();
            break;
        case BLE_FIRMWARE_OTA_GATT_ATTR_DATA:
        case BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES:
            value = listener_device_get_capabilities();
            break;
        case BLE_FIRMWARE_OTA_GATT_ATTR_V2_STATUS:
            return ble_firmware_ota_v2_append_status(ctxt->om);
        default:
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }

        value = ble_firmware_ota_compact_read_value_if_needed(conn_handle, attr, value);
        int rc = os_mbuf_append(ctxt->om, value, strlen(value));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        ESP_LOGI(TAG, "OTA identity read attr=%u value=%s", (unsigned)attr, value);
        return 0;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    switch (attr) {
    case BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL:
        if (ble_firmware_ota_v2_control_mbuf_has_magic(ctxt->om)) {
            return ble_firmware_ota_v2_handle_control_write(ctxt->om);
        }
        return ble_firmware_ota_handle_control_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_DATA:
        return ble_firmware_ota_handle_data_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_V2_CONTROL:
        return ble_firmware_ota_v2_handle_control_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_V2_DATA:
        return ble_firmware_ota_v2_handle_data_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_READINESS:
    case BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES:
    case BLE_FIRMWARE_OTA_GATT_ATTR_V2_STATUS:
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def s_ota_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_control_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_DATA,
            },
            {
                .uuid = &s_readiness_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_READINESS,
            },
            {
                .uuid = &s_capabilities_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES,
            },
            {
                .uuid = &s_v2_control_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_V2_CONTROL,
            },
            {
                .uuid = &s_v2_data_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_V2_DATA,
            },
            {
                .uuid = &s_v2_status_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_V2_STATUS,
            },
            {0},
        },
    },
    {0},
};

esp_err_t ble_firmware_ota_register_gatt(void)
{
    if (s_registered) {
        return ESP_OK;
    }

    ble_firmware_ota_v2_reset();

    int rc = ble_gatts_count_cfg(s_ota_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_ota_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    s_registered = true;
    ESP_LOGI(TAG, "firmware OTA GATT service registered");
    return ESP_OK;
}

void ble_firmware_ota_on_gap_disconnect(uint16_t conn_handle)
{
    (void)conn_handle;
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_DISCONNECT);
    ble_firmware_ota_v2_reset();
}

void ble_firmware_ota_log_gatt_state(void)
{
    uint16_t service_handle = 0;
    uint16_t control_def_handle = 0;
    uint16_t control_val_handle = 0;
    uint16_t data_def_handle = 0;
    uint16_t data_val_handle = 0;
    uint16_t readiness_def_handle = 0;
    uint16_t readiness_val_handle = 0;
    uint16_t capabilities_def_handle = 0;
    uint16_t capabilities_val_handle = 0;
    uint16_t v2_service_handle = 0;
    uint16_t v2_control_def_handle = 0;
    uint16_t v2_control_val_handle = 0;
    uint16_t v2_data_def_handle = 0;
    uint16_t v2_data_val_handle = 0;
    uint16_t v2_status_def_handle = 0;
    uint16_t v2_status_val_handle = 0;
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
        &data_val_handle);
    int readiness_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_readiness_uuid.u,
        &readiness_def_handle,
        &readiness_val_handle);
    int capabilities_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_capabilities_uuid.u,
        &capabilities_def_handle,
        &capabilities_val_handle);
    int v2_svc_rc = ble_gatts_find_svc(&s_service_uuid.u, &v2_service_handle);
    int v2_control_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_v2_control_uuid.u,
        &v2_control_def_handle,
        &v2_control_val_handle);
    int v2_data_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_v2_data_uuid.u,
        &v2_data_def_handle,
        &v2_data_val_handle);
    int v2_status_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_v2_status_uuid.u,
        &v2_status_def_handle,
        &v2_status_val_handle);

    ESP_LOGI(
        TAG,
        "firmware OTA GATT state: registered=%u svc_rc=%d svc_handle=%u control_rc=%d control_def=%u control_val=%u data_rc=%d data_def=%u data_val=%u readiness_rc=%d readiness_def=%u readiness_val=%u capabilities_rc=%d capabilities_def=%u capabilities_val=%u v2_svc_rc=%d v2_svc_handle=%u v2_control_rc=%d v2_control_def=%u v2_control_val=%u v2_data_rc=%d v2_data_def=%u v2_data_val=%u v2_status_rc=%d v2_status_def=%u v2_status_val=%u",
        s_registered,
        svc_rc,
        service_handle,
        control_rc,
        control_def_handle,
        control_val_handle,
        data_rc,
        data_def_handle,
        data_val_handle,
        readiness_rc,
        readiness_def_handle,
        readiness_val_handle,
        capabilities_rc,
        capabilities_def_handle,
        capabilities_val_handle,
        v2_svc_rc,
        v2_service_handle,
        v2_control_rc,
        v2_control_def_handle,
        v2_control_val_handle,
        v2_data_rc,
        v2_data_def_handle,
        v2_data_val_handle,
        v2_status_rc,
        v2_status_def_handle,
        v2_status_val_handle);
}
