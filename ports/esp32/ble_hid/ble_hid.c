#include "ble_hid.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_store.h"
void ble_store_config_init(void);
#else
#include "esp_gap_ble_api.h"
#endif

#include "hid_keyboard.h"
#include "board.h"
#include "ble_hid_gap.h"

static const char *TAG = "ble_hid";

typedef struct
{
    TaskHandle_t task_handle;
    esp_hidd_dev_t *hid_device;
} ble_hid_ctx_t;

static ble_hid_ctx_t s_ble_hid_ctx = {0};

static const char *s_device_name = "Listener Keyboard";

static esp_hid_raw_report_map_t s_ble_report_maps[] = {
    {
        .data = NULL,
        .len = 0,
    },
};

static esp_hid_device_config_t s_ble_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "Codex Keyboard",
    .manufacturer_name = "Codex",
    .serial_number = "keyboard-v1",
    .report_maps = s_ble_report_maps,
    .report_maps_len = 1,
};

static bool s_usb_serial_ready = false;

static esp_err_t ble_hid_usb_serial_init(void)
{
    if (s_usb_serial_ready) {
        return ESP_OK;
    }

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t ret = usb_serial_jtag_driver_install(&usb_config);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    s_usb_serial_ready = true;
    return ESP_OK;
}

static void ble_hid_keyboard_task(void *parameter)
{
    (void)parameter;

    char rx_buffer[16];
    esp_err_t ret = ble_hid_usb_serial_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB SERIAL INIT FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB SERIAL INPUT READY");
    board_print_help();
    while (1) {
        int bytes_read = usb_serial_jtag_read_bytes(rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(20));
        if (bytes_read > 0) {
            for (int index = 0; index < bytes_read; ++index) {
                int input_char = (unsigned char)rx_buffer[index];

            ESP_LOGI(
                TAG,
                "SCRIPT RX input=0x%02X display=%c",
                input_char & 0xFF,
                (input_char >= 32 && input_char <= 126) ? input_char : '.');

                ret = hid_keyboard_send_ascii((char)input_char, s_ble_hid_ctx.hid_device);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "SCRIPT dispatch failed: %s", esp_err_to_name(ret));
                }
            }
        }
    }
}

static void ble_hid_task_start(void)
{
    if (s_ble_hid_ctx.task_handle != NULL) {
        return;
    }

    xTaskCreate(
        ble_hid_keyboard_task,
        "ble_hid_keyboard_task",
        3 * 1024,
        NULL,
        configMAX_PRIORITIES - 3,
        &s_ble_hid_ctx.task_handle);
}

static void ble_hid_task_stop(void)
{
    if (s_ble_hid_ctx.task_handle != NULL) {
        vTaskDelete(s_ble_hid_ctx.task_handle);
        s_ble_hid_ctx.task_handle = NULL;
    }
}

void ble_hid_task_start_up(void)
{
    ble_hid_task_start();
}

void ble_hid_task_shut_down(void)
{
    ble_hid_task_stop();
}

static void ble_hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "START");
        ble_hid_gap_start_advertising();
        ble_hid_task_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "CONNECT");
        break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
        ESP_LOGI(
            TAG,
            "PROTOCOL MODE[%u]: %s",
            param->protocol_mode.map_index,
            param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        break;
    case ESP_HIDD_CONTROL_EVENT:
        ESP_LOGI(
            TAG,
            "CONTROL[%u]: %sSUSPEND",
            param->control.map_index,
            param->control.control ? "EXIT_" : "");
        if (param->control.control) {
            ble_hid_task_start();
        } else {
            ble_hid_task_stop();
        }
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(
            TAG,
            "OUTPUT[%u]: %8s ID: %2u, Len: %d",
            param->output.map_index,
            esp_hid_usage_str(param->output.usage),
            param->output.report_id,
            param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        break;
    case ESP_HIDD_FEATURE_EVENT:
        ESP_LOGI(
            TAG,
            "FEATURE[%u]: %8s ID: %2u, Len: %d",
            param->feature.map_index,
            esp_hid_usage_str(param->feature.usage),
            param->feature.report_id,
            param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(
            TAG,
            "DISCONNECT: %s",
            esp_hid_disconnect_reason_str(
                esp_hidd_dev_transport_get(param->disconnect.dev),
                param->disconnect.reason));
        ble_hid_gap_start_advertising();
        ble_hid_task_start();
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "STOP");
        break;
    default:
        break;
    }
}

#if CONFIG_BT_NIMBLE_ENABLED
static void ble_hid_host_task(void *parameter)
{
    (void)parameter;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}
#endif

void ble_hid_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_ble_hid_config.device_name = s_device_name;
    s_ble_report_maps[0].data = hid_keyboard_get_report_map();
    s_ble_report_maps[0].len = hid_keyboard_get_report_map_size();

    ESP_ERROR_CHECK(ble_hid_gap_init());

#if CONFIG_BT_NIMBLE_ENABLED
    ESP_ERROR_CHECK(ble_hid_gap_configure_advertising(ESP_HID_APPEARANCE_KEYBOARD, s_device_name));
#else
    ESP_ERROR_CHECK(ble_hid_gap_configure_advertising(ESP_HID_APPEARANCE_KEYBOARD, s_device_name));
    ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    ESP_ERROR_CHECK(ret);
#endif

    ESP_ERROR_CHECK(
        esp_hidd_dev_init(
            &s_ble_hid_config,
            ESP_HID_TRANSPORT_BLE,
            ble_hid_event_callback,
            &s_ble_hid_ctx.hid_device));
}

void ble_hid_start(void)
{
#if CONFIG_BT_NIMBLE_ENABLED
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    esp_err_t ret = esp_nimble_enable(ble_hid_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
#endif
}
