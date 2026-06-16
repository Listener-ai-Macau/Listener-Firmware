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
#define BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES "firmware_ota_v1"

typedef enum {
    BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL = 1,
    BLE_FIRMWARE_OTA_GATT_ATTR_DATA = 2,
    BLE_FIRMWARE_OTA_GATT_ATTR_READINESS = 3,
    BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES = 4,
} ble_firmware_ota_gatt_attr_t;

static const char *TAG = "ble_firmware_ota";
static const ble_uuid128_t s_service_uuid = BLE_FIRMWARE_OTA_SERVICE_UUID;
static const ble_uuid128_t s_control_uuid = BLE_FIRMWARE_OTA_CONTROL_UUID;
static const ble_uuid128_t s_data_uuid = BLE_FIRMWARE_OTA_DATA_UUID;
static const ble_uuid128_t s_readiness_uuid = BLE_FIRMWARE_OTA_READINESS_UUID;
static const ble_uuid128_t s_capabilities_uuid = BLE_FIRMWARE_OTA_CAPABILITIES_UUID;
static bool s_registered;

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

static int ble_firmware_ota_handle_control_write(struct os_mbuf *om)
{
    uint8_t raw[BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES + 1];
    uint16_t len = 0;
    int att_err = ble_firmware_ota_copy_mbuf(om, raw, BLE_FIRMWARE_OTA_CONTROL_MAX_BYTES, &len);
    if (att_err != 0) {
        return att_err;
    }
    raw[len] = '\0';

    char op[16];
    if (!ble_firmware_ota_json_string((const char *)raw, "op", op, sizeof(op))) {
        ESP_LOGW(TAG, "OTA control missing op");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (strcmp(op, "begin") == 0) {
        return ble_firmware_ota_handle_begin((const char *)raw);
    }
    if (strcmp(op, "finish") == 0) {
        return ble_firmware_ota_handle_finish((const char *)raw);
    }
    if (strcmp(op, "abort") == 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        ESP_LOGW(TAG, "OTA control abort requested");
        return 0;
    }

    ESP_LOGW(TAG, "OTA control unknown op=%s", op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int ble_firmware_ota_handle_data_write(struct os_mbuf *om)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t buffer[BLE_FIRMWARE_OTA_DATA_MAX_BYTES];
    int att_err = ble_firmware_ota_copy_mbuf(om, buffer, sizeof(buffer), &len);
    if (att_err != 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return att_err;
    }

    esp_err_t ret = firmware_ota_write(buffer, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA data write failed len=%u ret=%s", len, esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return ble_firmware_ota_att_error_from_esp(ret);
    }
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
        case BLE_FIRMWARE_OTA_GATT_ATTR_READINESS:
            value = listener_device_get_factory_readiness();
            break;
        case BLE_FIRMWARE_OTA_GATT_ATTR_DATA:
        case BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES:
            value = listener_device_get_capabilities();
            break;
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
        return ble_firmware_ota_handle_control_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_DATA:
        return ble_firmware_ota_handle_data_write(ctxt->om);
    case BLE_FIRMWARE_OTA_GATT_ATTR_READINESS:
    case BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES:
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

    ESP_LOGI(
        TAG,
        "firmware OTA GATT state: registered=%u svc_rc=%d svc_handle=%u control_rc=%d control_def=%u control_val=%u data_rc=%d data_def=%u data_val=%u readiness_rc=%d readiness_def=%u readiness_val=%u capabilities_rc=%d capabilities_def=%u capabilities_val=%u",
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
        capabilities_val_handle);
}
