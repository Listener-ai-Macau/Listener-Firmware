#include "ble_hid.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"
void ble_store_config_init(void);

#include "hid_keyboard.h"
#include "board.h"
#include "board_pins.h"
#include "listener_device.h"
#include "ble_hid_gap.h"
#include "ble_audio_stream.h"
#include "voice_recording_control.h"

static const char *TAG = "ble_hid";

#define BLE_HID_BATTERY_FALLBACK_LEVEL 50
#define BLE_HID_BATTERY_UPDATE_INTERVAL_MS 60000
#define BLE_HID_BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BLE_HID_BATTERY_ADC_RAW_MAX 4095U
#define BLE_HID_BATTERY_ADC_FALLBACK_REF_MV 3300U
#define BLE_HID_BATTERY_DIVIDER_NUMERATOR 2U
#define BLE_HID_BATTERY_DIVIDER_DENOMINATOR 1U
#define BLE_HID_BATTERY_EMPTY_MV 3000U
#define BLE_HID_BATTERY_FULL_MV 4200U
#define BLE_HID_BATTERY_TASK_STACK_BYTES (4 * 1024)

typedef struct
{
    TaskHandle_t task_handle;
    TaskHandle_t battery_task_handle;
    esp_hidd_dev_t *hid_device;
} ble_hid_ctx_t;

static ble_hid_ctx_t s_ble_hid_ctx = {0};

static const char *s_device_name = LISTENER_DEVICE_BLE_NAME;

static char s_ble_serial[18];
static adc_oneshot_unit_handle_t s_battery_adc_handle;
static adc_cali_handle_t s_battery_cali_handle;
static adc_unit_t s_battery_adc_unit = ADC_UNIT_1;
static adc_channel_t s_battery_adc_channel = ADC_CHANNEL_6;
static bool s_battery_adc_ready;
static bool s_battery_cali_ready;
static bool s_battery_adc_warned;

static esp_hid_raw_report_map_t s_ble_report_maps[] = {
    {
        .data = NULL,
        .len = 0,
    },
};

static esp_hid_device_config_t s_ble_hid_config = {
    .vendor_id = LISTENER_VENDOR_ID,
    .product_id = LISTENER_PRODUCT_ID,
    .version = LISTENER_PROTOCOL_VERSION,
    .device_name = LISTENER_DEVICE_BLE_NAME,
    .manufacturer_name = LISTENER_DEVICE_MANUFACTURER,
    .serial_number = s_ble_serial,
    .report_maps = s_ble_report_maps,
    .report_maps_len = 1,
};

static bool s_usb_serial_ready = false;

static bool ble_hid_battery_calibration_init(adc_unit_t unit, adc_channel_t channel)
{
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = BLE_HID_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&curve_config, &s_battery_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "battery ADC calibration: curve fitting");
        return true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = unit,
        .atten = BLE_HID_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&line_config, &s_battery_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "battery ADC calibration: line fitting");
        return true;
    }
#endif

    ESP_LOGW(TAG, "battery ADC calibration unavailable: %s", esp_err_to_name(ret));
    return false;
}

static esp_err_t ble_hid_battery_adc_init(void)
{
    if (s_battery_adc_ready) {
        return ESP_OK;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    esp_err_t ret = adc_oneshot_io_to_channel((int)BOARD_PINS_BAT_V_ADC_IO, &unit, &channel);
    if (ret != ESP_OK) {
        if (!s_battery_adc_warned) {
            ESP_LOGW(
                TAG,
                "battery ADC pin unavailable: gpio=%d ret=%s",
                (int)BOARD_PINS_BAT_V_ADC_IO,
                esp_err_to_name(ret));
            s_battery_adc_warned = true;
        }
        return ret;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_config, &s_battery_adc_handle);
    if (ret != ESP_OK) {
        if (!s_battery_adc_warned) {
            ESP_LOGW(TAG, "battery ADC unit init failed: %s", esp_err_to_name(ret));
            s_battery_adc_warned = true;
        }
        return ret;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = BLE_HID_BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_battery_adc_handle, channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC channel config failed: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_battery_adc_handle);
        s_battery_adc_handle = NULL;
        return ret;
    }

    s_battery_adc_unit = unit;
    s_battery_adc_channel = channel;
    s_battery_cali_ready = ble_hid_battery_calibration_init(unit, channel);
    s_battery_adc_ready = true;
    ESP_LOGI(
        TAG,
        "battery ADC ready: gpio=%d unit=%d channel=%d",
        (int)BOARD_PINS_BAT_V_ADC_IO,
        (int)s_battery_adc_unit,
        (int)s_battery_adc_channel);
    return ESP_OK;
}

static esp_err_t ble_hid_read_battery_mv(uint32_t *out_battery_mv, int *out_raw)
{
    if (out_battery_mv == NULL || out_raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ble_hid_battery_adc_init();
    if (ret != ESP_OK) {
        return ret;
    }

    int raw = 0;
    ret = adc_oneshot_read(s_battery_adc_handle, s_battery_adc_channel, &raw);
    if (ret != ESP_OK) {
        return ret;
    }

    int pad_mv = 0;
    if (s_battery_cali_ready) {
        ret = adc_cali_raw_to_voltage(s_battery_cali_handle, raw, &pad_mv);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        pad_mv = (int)(((uint32_t)raw * BLE_HID_BATTERY_ADC_FALLBACK_REF_MV) /
                       BLE_HID_BATTERY_ADC_RAW_MAX);
    }

    uint32_t battery_mv =
        ((uint32_t)pad_mv * BLE_HID_BATTERY_DIVIDER_NUMERATOR) /
        BLE_HID_BATTERY_DIVIDER_DENOMINATOR;
    *out_raw = raw;
    *out_battery_mv = battery_mv;
    return ESP_OK;
}

static uint8_t ble_hid_battery_percent_from_mv(uint32_t battery_mv)
{
    if (battery_mv <= BLE_HID_BATTERY_EMPTY_MV) {
        return 0;
    }
    if (battery_mv >= BLE_HID_BATTERY_FULL_MV) {
        return 100;
    }

    uint32_t range_mv = BLE_HID_BATTERY_FULL_MV - BLE_HID_BATTERY_EMPTY_MV;
    uint32_t level =
        ((battery_mv - BLE_HID_BATTERY_EMPTY_MV) * 100U + (range_mv / 2U)) /
        range_mv;
    return (uint8_t)level;
}

static void ble_hid_update_battery_level(const char *reason)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        return;
    }

    uint32_t battery_mv = 0;
    int raw = 0;
    esp_err_t read_ret = ble_hid_read_battery_mv(&battery_mv, &raw);
    uint8_t level = (read_ret == ESP_OK)
        ? ble_hid_battery_percent_from_mv(battery_mv)
        : BLE_HID_BATTERY_FALLBACK_LEVEL;

    esp_err_t ret = esp_hidd_dev_battery_set(s_ble_hid_ctx.hid_device, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery level update failed: %s", esp_err_to_name(ret));
        return;
    }

    if (read_ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "battery level=%u voltage_mv=%" PRIu32 " raw=%d reason=%s",
            level,
            battery_mv,
            raw,
            reason);
    } else {
        ESP_LOGW(
            TAG,
            "battery level fallback=%u reason=%s read_failed=%s",
            level,
            reason,
            esp_err_to_name(read_ret));
    }
}

static void ble_hid_battery_task(void *parameter)
{
    (void)parameter;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BLE_HID_BATTERY_UPDATE_INTERVAL_MS));
        ble_hid_update_battery_level("periodic");
    }
}

static void ble_hid_battery_task_start(void)
{
    if (s_ble_hid_ctx.battery_task_handle != NULL) {
        return;
    }

    BaseType_t task_ok = xTaskCreate(
        ble_hid_battery_task,
        "ble_hid_battery_task",
        BLE_HID_BATTERY_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 5,
        &s_ble_hid_ctx.battery_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGW(TAG, "battery update task create failed");
    }
}

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

                if (voice_recording_control_consume_usb_control_byte((uint8_t)input_char)) {
                    continue;
                }

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

static void ble_hid_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "START");
        ble_audio_stream_log_gatt_state();
        ble_hid_gap_mark_stack_ready();
        ble_hid_update_battery_level("hid_start");
        ble_hid_battery_task_start();
        ble_hid_task_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "CONNECT");
        ble_hid_update_battery_level("connect");
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
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "STOP");
        break;
    default:
        break;
    }
}

static void ble_hid_host_task(void *parameter)
{
    (void)parameter;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_hid_log_dis_result(const char *field, int rc)
{
    if (rc != 0) {
        ESP_LOGW(TAG, "DIS %s set failed: rc=%d", field, rc);
    }
}

static void ble_hid_configure_dis_identity(void)
{
    ble_hid_log_dis_result("model", ble_svc_dis_model_number_set(LISTENER_DEVICE_MODEL));
    ble_hid_log_dis_result("hardware_revision", ble_svc_dis_hardware_revision_set(LISTENER_DEVICE_HW_REV));
    ble_hid_log_dis_result("firmware_revision", ble_svc_dis_firmware_revision_set(listener_device_get_fw_version()));
    ble_hid_log_dis_result("software_revision", ble_svc_dis_software_revision_set(listener_device_get_protocol_version()));

    ESP_LOGI(TAG,
             "DIS identity: manufacturer=%s model=%s hw=%s fw=%s proto=%s serial=%s",
             LISTENER_DEVICE_MANUFACTURER,
             LISTENER_DEVICE_MODEL,
             LISTENER_DEVICE_HW_REV,
             listener_device_get_fw_version(),
             listener_device_get_protocol_version(),
             listener_device_get_serial());
}

void ble_hid_init(void)
{
    ESP_LOGI(TAG, "fw_version=%s protocol_version=%u build=%s serial=%s",
             listener_device_get_fw_version(),
             LISTENER_PROTOCOL_VERSION,
             listener_device_get_build_id(),
             listener_device_get_serial());
    ESP_LOGI(TAG,
             "factory readiness: ble_name=%s appearance=0x%04x readiness=%s capabilities=%s",
             listener_device_get_ble_name(),
             LISTENER_DEVICE_BLE_APPEARANCE_KEYBOARD,
             listener_device_get_factory_readiness(),
             listener_device_get_capabilities());

    snprintf(s_ble_serial, sizeof(s_ble_serial), "%s", listener_device_get_serial());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_ble_report_maps[0].data = hid_keyboard_get_report_map();
    s_ble_report_maps[0].len = hid_keyboard_get_report_map_size();

    ESP_ERROR_CHECK(ble_hid_gap_init());
    ESP_ERROR_CHECK(ble_audio_stream_init());
    ESP_ERROR_CHECK(ble_audio_stream_register_gatt());

    ESP_ERROR_CHECK(ble_hid_gap_configure_advertising(ESP_HID_APPEARANCE_KEYBOARD, s_device_name));

    ESP_ERROR_CHECK(
        esp_hidd_dev_init(
            &s_ble_hid_config,
            ESP_HID_TRANSPORT_BLE,
            ble_hid_event_callback,
            &s_ble_hid_ctx.hid_device));

    ble_hid_configure_dis_identity();
    ble_audio_stream_log_gatt_state();

    int gap_name_rc = ble_svc_gap_device_name_set(s_device_name);
    if (gap_name_rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", gap_name_rc);
    }

    ble_hid_update_battery_level("init");
}

void ble_hid_start(void)
{
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    esp_err_t ret = esp_nimble_enable(ble_hid_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
}
