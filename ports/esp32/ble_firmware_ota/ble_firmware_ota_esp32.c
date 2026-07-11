#include "ble_firmware_ota.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "denzic_ota_v1.h"
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

#define BLE_FIRMWARE_OTA_DATA_MAX_BYTES 512
#define BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX 500
#define BLE_FIRMWARE_OTA_DEFAULT_WINDOW_CHUNKS 8
#define BLE_FIRMWARE_OTA_ACTIVE_LINK_RETRY_MS 250
#define BLE_FIRMWARE_OTA_REBOOT_DELAY_MS 500
#define BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES 2048
#define BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES DENZIC_OTA_V1_PROTOCOL_NAME

typedef enum {
    BLE_FIRMWARE_OTA_GATT_ATTR_READINESS = 1,
    BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES = 2,
    BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL = 3,
    BLE_FIRMWARE_OTA_GATT_ATTR_DATA = 4,
    BLE_FIRMWARE_OTA_GATT_ATTR_STATUS = 5,
} ble_firmware_ota_gatt_attr_t;

static const char *TAG = "ble_firmware_ota";
static const ble_uuid128_t s_service_uuid = BLE_FIRMWARE_OTA_SERVICE_UUID;
static const ble_uuid128_t s_readiness_uuid = BLE_FIRMWARE_OTA_READINESS_UUID;
static const ble_uuid128_t s_capabilities_uuid = BLE_FIRMWARE_OTA_CAPABILITIES_UUID;
static const ble_uuid128_t s_control_uuid = BLE_FIRMWARE_OTA_V1_CONTROL_UUID;
static const ble_uuid128_t s_data_uuid = BLE_FIRMWARE_OTA_V1_DATA_UUID;
static const ble_uuid128_t s_status_uuid = BLE_FIRMWARE_OTA_V1_STATUS_UUID;
static denzic_ota_v1_context_t s_ota;
static TickType_t s_active_link_retry_tick;
static bool s_active_link_confirmed;
static bool s_registered;

extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_schedule_active_connection(void) __attribute__((weak));
extern bool ble_hid_gap_active_connection_applied(void) __attribute__((weak));

static int ble_firmware_ota_att_error_from_core(void)
{
    switch (s_ota.last_error) {
    case DENZIC_OTA_V1_ERROR_BAD_MAGIC:
    case DENZIC_OTA_V1_ERROR_BAD_SIZE:
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    case DENZIC_OTA_V1_ERROR_OFFSET_MISMATCH:
        return 0;
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

static void ble_firmware_ota_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DELAY_MS));
    firmware_ota_reboot_to_pending_image();
    vTaskDelete(NULL);
}

static bool ble_firmware_ota_schedule_reboot(void)
{
    BaseType_t started = xTaskCreate(
        ble_firmware_ota_reboot_task,
        "ble_ota_reboot",
        BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL);
    return started == pdPASS;
}

static bool ble_firmware_ota_storage_begin(void *driver_context, uint32_t image_size)
{
    (void)driver_context;
    s_active_link_retry_tick = 0;
    s_active_link_confirmed = false;
    esp_err_t ret = firmware_ota_begin(image_size, DENZIC_OTA_V1_PROTOCOL_NAME);
    ESP_LOGI(TAG, "Denzic OTA v1 begin size=%u ret=%s", (unsigned)image_size, esp_err_to_name(ret));
    return ret == ESP_OK;
}

static bool ble_firmware_ota_storage_write(
    void *driver_context,
    const uint8_t *data,
    size_t length)
{
    (void)driver_context;
    esp_err_t ret = firmware_ota_write(data, length);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Denzic OTA v1 data write failed len=%u ret=%s", (unsigned)length, esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
    }
    return ret == ESP_OK;
}

static bool ble_firmware_ota_storage_finish(void *driver_context)
{
    (void)driver_context;
    esp_err_t ret = firmware_ota_finish(false);
    ESP_LOGI(TAG, "Denzic OTA v1 finish size=%u ret=%s", (unsigned)s_ota.expected_size, esp_err_to_name(ret));
    if (ret != ESP_OK) {
        return false;
    }
    if (!ble_firmware_ota_schedule_reboot()) {
        ESP_LOGE(TAG, "Denzic OTA v1 reboot scheduling failed");
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
        return false;
    }
    return true;
}

static void ble_firmware_ota_storage_abort(void *driver_context)
{
    (void)driver_context;
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
}

static void ble_firmware_ota_core_init(void)
{
    const denzic_ota_v1_storage_driver_t storage = {
        .begin = ble_firmware_ota_storage_begin,
        .write = ble_firmware_ota_storage_write,
        .finish = ble_firmware_ota_storage_finish,
        .abort = ble_firmware_ota_storage_abort,
    };
    denzic_ota_v1_init(
        &s_ota,
        storage,
        NULL,
        BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX,
        BLE_FIRMWARE_OTA_DEFAULT_WINDOW_CHUNKS);
    s_active_link_retry_tick = 0;
    s_active_link_confirmed = false;
}

static bool ble_firmware_ota_tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void ble_firmware_ota_update_active_link(void)
{
    if (s_ota.state != DENZIC_OTA_V1_STATE_RECEIVING) {
        s_active_link_confirmed = false;
        denzic_ota_v1_set_status_flags(&s_ota, 0);
        return;
    }

    if (ble_hid_gap_active_connection_applied == NULL ||
        ble_hid_gap_active_connection_applied()) {
        if (!s_active_link_confirmed) {
            ESP_LOGI(TAG, "Denzic OTA v1 active BLE link confirmed");
        }
        s_active_link_confirmed = true;
        denzic_ota_v1_set_status_flags(
            &s_ota,
            DENZIC_OTA_V1_STATUS_FLAG_ACTIVE_LINK_CONFIRMED);
        return;
    }

    denzic_ota_v1_set_status_flags(&s_ota, 0);
    TickType_t now = xTaskGetTickCount();
    if (s_active_link_retry_tick != 0 &&
        !ble_firmware_ota_tick_reached(now, s_active_link_retry_tick)) {
        return;
    }

    s_active_link_retry_tick = now + pdMS_TO_TICKS(BLE_FIRMWARE_OTA_ACTIVE_LINK_RETRY_MS);
    if (ble_hid_gap_schedule_active_connection != NULL ||
        ble_hid_gap_request_active_connection != NULL) {
        esp_err_t ret = ble_hid_gap_schedule_active_connection != NULL
            ? ble_hid_gap_schedule_active_connection()
            : ble_hid_gap_request_active_connection();
        ESP_LOGI(TAG, "Denzic OTA v1 active BLE link pending ret=%s", esp_err_to_name(ret));
    }
}

static void ble_firmware_ota_sync_status(void)
{
    ble_firmware_ota_update_active_link();
    firmware_ota_status_t status = firmware_ota_get_status();
    if (s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING && status.active) {
        s_ota.bytes_written = (uint32_t)status.bytes_written;
        s_ota.expected_size = (uint32_t)status.expected_size;
    }
}

static int ble_firmware_ota_append_status(struct os_mbuf *om)
{
    uint8_t status[DENZIC_OTA_V1_STATUS_BYTES];
    ble_firmware_ota_sync_status();
    size_t length = denzic_ota_v1_encode_status(&s_ota, status, sizeof(status));
    if (length == 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(om, status, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ble_firmware_ota_handle_control_write(struct os_mbuf *om)
{
    uint8_t control[DENZIC_OTA_V1_CONTROL_BYTES];
    uint16_t length = 0;
    int att_error = ble_firmware_ota_copy_mbuf(om, control, sizeof(control), &length);
    if (att_error != 0) {
        return att_error;
    }

    bool accepted = denzic_ota_v1_handle_control(&s_ota, control, length);
    ble_firmware_ota_update_active_link();
    if (!accepted) {
        ESP_LOGW(
            TAG,
            "Denzic OTA v1 control rejected op=%u error=%u len=%u",
            length > 4 ? control[4] : 0,
            s_ota.last_error,
            length);
        return ble_firmware_ota_att_error_from_core();
    }
    return 0;
}

static int ble_firmware_ota_handle_data_write(struct os_mbuf *om)
{
    uint8_t data[BLE_FIRMWARE_OTA_DATA_MAX_BYTES];
    uint16_t length = 0;
    int att_error = ble_firmware_ota_copy_mbuf(om, data, sizeof(data), &length);
    if (att_error != 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return att_error;
    }

    ble_firmware_ota_sync_status();
    if (!denzic_ota_v1_handle_data(&s_ota, data, length)) {
        return ble_firmware_ota_att_error_from_core();
    }
    return 0;
}

static const char *ble_firmware_ota_compact_capabilities_if_needed(
    uint16_t conn_handle,
    const char *value)
{
    if (value == NULL) {
        return "";
    }
    uint16_t mtu = ble_att_mtu(conn_handle);
    uint16_t value_max = mtu > 1 ? (uint16_t)(mtu - 1U) : 0U;
    if (mtu <= BLE_ATT_MTU_DFLT && strlen(value) > value_max) {
        return BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES;
    }
    return value;
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
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_STATUS) {
            return ble_firmware_ota_append_status(ctxt->om);
        }

        const char *value = NULL;
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_READINESS) {
            value = listener_device_get_factory_readiness();
        } else if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES) {
            value = ble_firmware_ota_compact_capabilities_if_needed(
                conn_handle,
                listener_device_get_capabilities());
        } else {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        return os_mbuf_append(ctxt->om, value, strlen(value)) == 0
            ? 0
            : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL) {
        return ble_firmware_ota_handle_control_write(ctxt->om);
    }
    if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_DATA) {
        return ble_firmware_ota_handle_data_write(ctxt->om);
    }
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def s_ota_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
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
                .uuid = &s_control_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_DATA,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_STATUS,
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

    ble_firmware_ota_core_init();
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
    ESP_LOGI(TAG, "Denzic OTA v1 GATT driver registered");
    return ESP_OK;
}

void ble_firmware_ota_on_gap_disconnect(uint16_t conn_handle)
{
    (void)conn_handle;
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_DISCONNECT);
    ble_firmware_ota_core_init();
}

void ble_firmware_ota_on_firmware_abort(uint32_t reason)
{
    if (s_ota.state != DENZIC_OTA_V1_STATE_IDLE) {
        ESP_LOGW(TAG, "Denzic OTA v1 session reset after firmware abort reason=%u", (unsigned)reason);
    }
    ble_firmware_ota_core_init();
}

void ble_firmware_ota_log_gatt_state(void)
{
    uint16_t service_handle = 0;
    uint16_t control_def_handle = 0;
    uint16_t control_val_handle = 0;
    uint16_t data_def_handle = 0;
    uint16_t data_val_handle = 0;
    uint16_t status_def_handle = 0;
    uint16_t status_val_handle = 0;
    int service_rc = ble_gatts_find_svc(&s_service_uuid.u, &service_handle);
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
    int status_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_status_uuid.u,
        &status_def_handle,
        &status_val_handle);

    ESP_LOGI(
        TAG,
        "Denzic OTA v1 GATT state: registered=%u service_rc=%d service=%u control_rc=%d control_def=%u control_val=%u data_rc=%d data_def=%u data_val=%u status_rc=%d status_def=%u status_val=%u",
        s_registered,
        service_rc,
        service_handle,
        control_rc,
        control_def_handle,
        control_val_handle,
        data_rc,
        data_def_handle,
        data_val_handle,
        status_rc,
        status_def_handle,
        status_val_handle);
}
