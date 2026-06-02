#include "ble_hid.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"
void ble_store_config_init(void);

#include "hid_keyboard.h"
#include "audio_capture.h"
#include "battery_monitor.h"
#include "board.h"
#include "boot_safety.h"
#include "listener_device.h"
#include "ble_hid_gap.h"
#include "ble_audio_stream.h"
#include "ble_firmware_ota.h"
#include "ble_diag_log.h"
#include "voice_recording_control.h"
#include "diag_log_platform.h"
#include "diag_log.h"
#include "firmware_ota.h"
#include "power_manager.h"
#include "status_led.h"
#include "watchdog_platform.h"
#include "esp_timer.h"

static const char *TAG = "ble_hid";

#define BLE_HID_BATTERY_FALLBACK_LEVEL 50
#define BLE_HID_BATTERY_SAMPLE_INTERVAL_MS 5000
#define BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT 1
#define BLE_HID_BATTERY_LEVEL_INVALID UINT8_MAX
#define BLE_HID_BATTERY_TASK_STACK_BYTES (4 * 1024)
#define BLE_HID_USB_COMMAND_PREFIX '~'
#define BLE_HID_USB_COMMAND_BUFFER_BYTES 64
#define BLE_HID_ASCII_QUEUE_LENGTH 8
#define BLE_HID_USAGE_QUEUE_LENGTH 8
#define BLE_HID_KEY_SOURCE_BYTES 32
#define BLE_HID_READINESS_ALL \
    (LISTENER_DEVICE_READY_HID | LISTENER_DEVICE_READY_AUDIO | \
     LISTENER_DEVICE_READY_OTA | LISTENER_DEVICE_READY_DIAGNOSTIC)

typedef struct
{
    TaskHandle_t task_handle;
    TaskHandle_t battery_task_handle;
    esp_hidd_dev_t *hid_device;
} ble_hid_ctx_t;

typedef struct {
    uint8_t usage;
    char source[BLE_HID_KEY_SOURCE_BYTES];
} ble_hid_usage_event_t;

static ble_hid_ctx_t s_ble_hid_ctx = {0};

static const char *s_device_name = LISTENER_DEVICE_BLE_NAME;

static char s_ble_serial[18];

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
static bool s_usb_command_active;
static size_t s_usb_command_length;
static char s_usb_command_buffer[BLE_HID_USB_COMMAND_BUFFER_BYTES];

static bool s_ble_connected;
static uint32_t s_disconnect_count;
static portMUX_TYPE s_disconnect_count_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_connect_timestamp_ms;
static QueueHandle_t s_ascii_queue;
static QueueHandle_t s_usage_queue;
static bool s_safe_mode;
static bool s_battery_service_valid;
static uint8_t s_battery_service_level = BLE_HID_BATTERY_LEVEL_INVALID;

static void ble_hid_log_dis_gatt_state(void);

static uint32_t ble_hid_disconnect_count_snapshot(void)
{
    portENTER_CRITICAL(&s_disconnect_count_lock);
    uint32_t count = s_disconnect_count;
    portEXIT_CRITICAL(&s_disconnect_count_lock);
    return count;
}

static uint32_t ble_hid_increment_disconnect_count(void)
{
    portENTER_CRITICAL(&s_disconnect_count_lock);
    if (s_disconnect_count != UINT32_MAX) {
        s_disconnect_count++;
    }
    uint32_t count = s_disconnect_count;
    portEXIT_CRITICAL(&s_disconnect_count_lock);
    return count;
}

static void ble_hid_publish_readiness(
    uint32_t ready_mask,
    uint32_t degraded_mask,
    const char *reason)
{
    listener_device_set_readiness(ready_mask, degraded_mask);
    ESP_LOGI(
        TAG,
        "device readiness: reason=%s ready_mask=0x%08" PRIx32
        " degraded_mask=0x%08" PRIx32 " readiness=%s capabilities=%s",
        reason != NULL ? reason : "unspecified",
        ready_mask,
        degraded_mask,
        listener_device_get_factory_readiness(),
        listener_device_get_capabilities());
}

static bool ble_hid_battery_level_exceeds_notify_threshold(uint8_t level)
{
    if (!s_battery_service_valid || s_battery_service_level == BLE_HID_BATTERY_LEVEL_INVALID) {
        return true;
    }

    uint8_t delta = level > s_battery_service_level
        ? (uint8_t)(level - s_battery_service_level)
        : (uint8_t)(s_battery_service_level - level);
    return delta > BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT;
}

static void ble_hid_update_battery_level(const char *reason, bool force_notify)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        return;
    }

    battery_monitor_status_t battery = {0};
    esp_err_t read_ret = battery_monitor_read(&battery);
    uint8_t level = (read_ret == ESP_OK && battery.valid)
        ? battery.level_percent
        : BLE_HID_BATTERY_FALLBACK_LEVEL;
    bool should_notify = force_notify ||
        (read_ret == ESP_OK && battery.valid &&
         ble_hid_battery_level_exceeds_notify_threshold(level));

    firmware_ota_note_battery(
        level,
        (read_ret == ESP_OK && battery.valid) ? battery.voltage_mv : 0,
        read_ret == ESP_OK && battery.valid);

    if (!should_notify) {
        ESP_LOGD(
            TAG,
            "battery unchanged level=%u last=%u reason=%s",
            level,
            s_battery_service_level,
            reason != NULL ? reason : "unspecified");
        return;
    }

    if (read_ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "battery notify level=%u voltage_mv=%" PRIu32 " raw=%d adc_mv=%d reason=%s forced=%u",
            level,
            battery.voltage_mv,
            battery.raw_adc,
            battery.adc_mv,
            reason != NULL ? reason : "unspecified",
            force_notify ? 1u : 0u);
    } else {
        ESP_LOGW(
            TAG,
            "battery notify fallback=%u reason=%s read_failed=%s forced=%u",
            level,
            reason != NULL ? reason : "unspecified",
            esp_err_to_name(read_ret),
            force_notify ? 1u : 0u);
    }

    esp_err_t ret = esp_hidd_dev_battery_set(s_ble_hid_ctx.hid_device, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery level notify failed: %s", esp_err_to_name(ret));
        return;
    }

    s_battery_service_level = level;
    s_battery_service_valid = true;

    if (read_ret == ESP_OK && battery.valid) {
        diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_BATTERY_LEVEL, DIAG_SEV_INFO,
                 level, battery.voltage_mv, (uint32_t)battery.raw_adc,
                 (uint32_t)battery.adc_mv);
    }

    if (level < 10 && read_ret == ESP_OK) {
        diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_BATTERY_WARN, DIAG_SEV_WARN,
                 level, battery.voltage_mv, (uint32_t)battery.raw_adc,
                 (uint32_t)battery.adc_mv);
    }
}

static void ble_hid_battery_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("ble_hid_battery_task");

    while (1) {
        watchdog_platform_delay_ms(BLE_HID_BATTERY_SAMPLE_INTERVAL_MS);
        ble_hid_update_battery_level("threshold_sample", false);
        watchdog_platform_feed_current_task();
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

static esp_err_t ble_hid_dispatch_ascii(char input_char, const char *source)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID device unavailable", source);
        return ESP_ERR_INVALID_STATE;
    }

    if (!esp_hidd_dev_connected(s_ble_hid_ctx.hid_device)) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID host not connected", source);
        return ESP_ERR_INVALID_STATE;
    }

    return hid_keyboard_send_ascii(input_char, s_ble_hid_ctx.hid_device);
}

static esp_err_t ble_hid_dispatch_usage(uint8_t usage, const char *source)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID device unavailable", source);
        return ESP_ERR_INVALID_STATE;
    }

    if (!esp_hidd_dev_connected(s_ble_hid_ctx.hid_device)) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID host not connected", source);
        return ESP_ERR_INVALID_STATE;
    }

    return hid_keyboard_send_usage(usage, s_ble_hid_ctx.hid_device);
}

static void ble_hid_drain_ascii_queue(void)
{
    if (s_ascii_queue == NULL) {
        return;
    }

    char input_char;
    while (xQueueReceive(s_ascii_queue, &input_char, 0) == pdTRUE) {
        esp_err_t ret = ble_hid_dispatch_ascii(input_char, "KEY");
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "KEY dispatch failed: %s", esp_err_to_name(ret));
        }
    }
}

static void ble_hid_drain_usage_queue(void)
{
    if (s_usage_queue == NULL) {
        return;
    }

    ble_hid_usage_event_t event;
    while (xQueueReceive(s_usage_queue, &event, 0) == pdTRUE) {
        const char *source = event.source[0] != '\0' ? event.source : "CUSTOM_KEY";
        esp_err_t ret = ble_hid_dispatch_usage(event.usage, source);
        if (ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "%s dispatch failed: usage=0x%02X error=%s",
                source,
                event.usage,
                esp_err_to_name(ret));
        }
    }
}

esp_err_t ble_hid_send_ascii_async(char input_char)
{
    if (s_ascii_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ble_hid_is_connected()) {
        (void)ble_hid_gap_request_reconnect();
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_ascii_queue, &input_char, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    power_manager_record_activity("hid_key_enqueue");
    return ESP_OK;
}

esp_err_t ble_hid_send_keyboard_usage_async(uint8_t usage, const char *source)
{
    if (s_usage_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ble_hid_is_connected()) {
        (void)ble_hid_gap_request_reconnect();
        return ESP_ERR_INVALID_STATE;
    }

    ble_hid_usage_event_t event = {
        .usage = usage,
    };
    if (source != NULL) {
        snprintf(event.source, sizeof(event.source), "%s", source);
    }

    if (xQueueSend(s_usage_queue, &event, 0) != pdTRUE) {
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_QUEUE_DROP, DIAG_SEV_WARN,
                 usage, BLE_HID_USAGE_QUEUE_LENGTH, 0, 0);
        return ESP_ERR_TIMEOUT;
    }

    power_manager_record_activity("hid_usage_enqueue");
    return ESP_OK;
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

static void ble_hid_dispatch_voice_recording_command(const char *line)
{
    for (const char *cursor = line; cursor != NULL && *cursor != '\0'; cursor++) {
        voice_recording_control_consume_usb_control_byte((uint8_t)*cursor);
    }
    voice_recording_control_consume_usb_control_byte((uint8_t)'\n');
}

static bool ble_hid_dispatch_usb_command_line(const char *line)
{
    power_manager_record_activity("usb_control_line");

    if (power_manager_consume_usb_command(line)) {
        return true;
    }

    if (board_consume_usb_command(line)) {
        return true;
    }

    if (status_led_consume_usb_command(line)) {
        return true;
    }

    if (watchdog_platform_consume_usb_command(line)) {
        return true;
    }

    if (boot_safety_consume_usb_command(line)) {
        return true;
    }

    if (strcmp(line, "~OTA:GATT") == 0 || strcmp(line, "OTA:GATT") == 0) {
        ble_firmware_ota_log_gatt_state();
        return true;
    }

    if (strcmp(line, "~DIS:GATT") == 0 || strcmp(line, "DIS:GATT") == 0) {
        ble_hid_log_dis_gatt_state();
        return true;
    }

    if (strcmp(line, "~DIAG:GATT") == 0 || strcmp(line, "DIAG:GATT") == 0) {
        ble_diag_log_log_gatt_state();
        return true;
    }

    if (strncmp(line, "~OTA:", strlen("~OTA:")) == 0) {
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_FLASH_WRITE |
            POWER_MANAGER_BLOCKER_USB_COMMAND,
            true);
        bool consumed = firmware_ota_consume_usb_command(line);
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_FLASH_WRITE |
            POWER_MANAGER_BLOCKER_USB_COMMAND,
            false);
        return consumed;
    }

    if (strncmp(line, "~DIAGLOG:", strlen("~DIAGLOG:")) == 0) {
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_DIAG_EXPORT |
            POWER_MANAGER_BLOCKER_FLASH_WRITE |
            POWER_MANAGER_BLOCKER_USB_COMMAND,
            true);
        bool consumed = diag_log_consume_usb_command(line);
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_DIAG_EXPORT |
            POWER_MANAGER_BLOCKER_FLASH_WRITE |
            POWER_MANAGER_BLOCKER_USB_COMMAND,
            false);
        return consumed;
    }

    if (diag_log_consume_usb_command(line)) {
        return true;
    }

    if (strncmp(line, "~VREC:", strlen("~VREC:")) == 0) {
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, true);
        ble_hid_dispatch_voice_recording_command(line);
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
        return true;
    }

    ESP_LOGW(TAG, "drop unknown USB control command: %s", line);
    return true;
}

static bool ble_hid_consume_usb_command_byte(uint8_t input_char)
{
    if (!s_usb_command_active) {
        if (input_char != (uint8_t)BLE_HID_USB_COMMAND_PREFIX) {
            return false;
        }

        s_usb_command_active = true;
        s_usb_command_length = 0;
        memset(s_usb_command_buffer, 0, sizeof(s_usb_command_buffer));
        s_usb_command_buffer[s_usb_command_length++] = (char)input_char;
        return true;
    }

    if (input_char == '\r') {
        return true;
    }

    if (input_char == '\n') {
        s_usb_command_active = false;
        s_usb_command_buffer[s_usb_command_length] = '\0';
        return ble_hid_dispatch_usb_command_line(s_usb_command_buffer);
    }

    if (s_usb_command_length + 1 >= sizeof(s_usb_command_buffer)) {
        s_usb_command_active = false;
        s_usb_command_length = 0;
        memset(s_usb_command_buffer, 0, sizeof(s_usb_command_buffer));
        ESP_LOGW(TAG, "drop USB control command: too long");
        return true;
    }

    s_usb_command_buffer[s_usb_command_length++] = (char)input_char;
    return true;
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
    (void)watchdog_platform_subscribe_current_task("ble_hid_keyboard_task");
    while (1) {
        watchdog_platform_feed_current_task();
        ble_hid_drain_ascii_queue();
        ble_hid_drain_usage_queue();

        int bytes_read = usb_serial_jtag_read_bytes(rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(20));
        if (bytes_read > 0) {
            for (int index = 0; index < bytes_read; ++index) {
                int input_char = (unsigned char)rx_buffer[index];

                if (ble_hid_consume_usb_command_byte((uint8_t)input_char)) {
                    continue;
                }

                ESP_LOGI(
                    TAG,
                    "SCRIPT RX input=0x%02X display=%c",
                    input_char & 0xFF,
                    (input_char >= 32 && input_char <= 126) ? input_char : '.');

                power_manager_record_activity("usb_ascii");
                ret = ble_hid_dispatch_ascii((char)input_char, "SCRIPT");
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "SCRIPT dispatch failed: %s", esp_err_to_name(ret));
                    diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_HID_SEND_FAIL, DIAG_SEV_WARN,
                             (uint32_t)input_char, (uint32_t)ret, s_ble_connected ? 1 : 0, 0);
                }
            }
        }
        watchdog_platform_feed_current_task();
    }
}

static void ble_hid_task_start(void)
{
    if (s_ble_hid_ctx.task_handle != NULL) {
        return;
    }

    BaseType_t task_ok = xTaskCreate(
        ble_hid_keyboard_task,
        "ble_hid_keyboard_task",
        3 * 1024,
        NULL,
        configMAX_PRIORITIES - 3,
        &s_ble_hid_ctx.task_handle);
    if (task_ok != pdPASS) {
        s_ble_hid_ctx.task_handle = NULL;
        ESP_LOGE(TAG, "failed to start BLE HID keyboard task");
    }
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
        power_manager_record_activity("ble_hid_start");
        status_led_show_status_window("ble_hid_start");
        if (!s_safe_mode) {
            ble_audio_stream_log_gatt_state();
        }
        ble_hid_gap_mark_stack_ready();
        ble_hid_update_battery_level("hid_start", true);
        ble_hid_battery_task_start();
        ble_hid_task_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "CONNECT");
        s_ble_connected = true;
        power_manager_set_ble_connected(true);
        status_led_set_ble_state(STATUS_LED_BLE_CONNECTED, true);
        s_connect_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        ble_hid_update_battery_level("connect_restore", true);
        uint32_t disconnect_count = ble_hid_disconnect_count_snapshot();
        diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_CONNECT, DIAG_SEV_INFO,
                 1, esp_get_free_heap_size() / 1024, disconnect_count, 0);
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
        {
            s_ble_connected = false;
            uint32_t disconnect_count = ble_hid_increment_disconnect_count();
            uint32_t conn_duration = (uint32_t)(esp_timer_get_time() / 1000LL) - s_connect_timestamp_ms;
            uint32_t heap_kb = esp_get_free_heap_size() / 1024;
            ESP_LOGI(TAG, "DISCONNECT: %s",
                     esp_hid_disconnect_reason_str(
                         esp_hidd_dev_transport_get(param->disconnect.dev),
                         param->disconnect.reason));
            diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_DISCONNECT, DIAG_SEV_WARN,
                     param->disconnect.reason, disconnect_count,
                     conn_duration, heap_kb);
            power_manager_set_ble_connected(false);
            ble_diag_log_on_gap_disconnect(0);
            ble_firmware_ota_on_gap_disconnect(0);
            status_led_set_ble_state(STATUS_LED_BLE_RECONNECTING, false);
            if (s_ascii_queue != NULL) {
                xQueueReset(s_ascii_queue);
            }
            if (s_usage_queue != NULL) {
                xQueueReset(s_usage_queue);
            }
        }
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
    ble_hid_log_dis_result("manufacturer", ble_svc_dis_manufacturer_name_set(LISTENER_DEVICE_MANUFACTURER));
    ble_hid_log_dis_result("model", ble_svc_dis_model_number_set(LISTENER_DEVICE_MODEL));
    ble_hid_log_dis_result("serial", ble_svc_dis_serial_number_set(listener_device_get_serial()));
    ble_hid_log_dis_result("hardware_revision", ble_svc_dis_hardware_revision_set(LISTENER_DEVICE_HW_REV));
    ble_hid_log_dis_result("firmware_revision", ble_svc_dis_firmware_revision_set(listener_device_get_fw_version()));
    ble_hid_log_dis_result("software_revision", ble_svc_dis_software_revision_set(listener_device_get_protocol_version()));

    ESP_LOGI(TAG,
             "DIS identity: manufacturer=%s model=%s hw=%s fw=%s proto=%s serial=%s vid=0x%04x pid=0x%04x product_version=%u",
             LISTENER_DEVICE_MANUFACTURER,
             LISTENER_DEVICE_MODEL,
             LISTENER_DEVICE_HW_REV,
             listener_device_get_fw_version(),
             listener_device_get_protocol_version(),
             listener_device_get_serial(),
             LISTENER_VENDOR_ID,
             LISTENER_PRODUCT_ID,
             LISTENER_PROTOCOL_VERSION);
}

static void ble_hid_log_dis_gatt_state(void)
{
    const ble_uuid16_t dis_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_UUID16);
    const ble_uuid16_t model_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_MODEL_NUMBER);
    const ble_uuid16_t serial_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_SERIAL_NUMBER);
    const ble_uuid16_t firmware_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_FIRMWARE_REVISION);
    const ble_uuid16_t hardware_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_HARDWARE_REVISION);
    const ble_uuid16_t software_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_SOFTWARE_REVISION);
    const ble_uuid16_t manufacturer_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME);
    const ble_uuid16_t pnp_uuid = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_PNP_ID);
    uint16_t service_handle = 0;
    uint16_t model_def_handle = 0;
    uint16_t model_val_handle = 0;
    uint16_t serial_def_handle = 0;
    uint16_t serial_val_handle = 0;
    uint16_t firmware_def_handle = 0;
    uint16_t firmware_val_handle = 0;
    uint16_t hardware_def_handle = 0;
    uint16_t hardware_val_handle = 0;
    uint16_t software_def_handle = 0;
    uint16_t software_val_handle = 0;
    uint16_t manufacturer_def_handle = 0;
    uint16_t manufacturer_val_handle = 0;
    uint16_t pnp_def_handle = 0;
    uint16_t pnp_val_handle = 0;

    int svc_rc = ble_gatts_find_svc(&dis_uuid.u, &service_handle);
    int model_rc = ble_gatts_find_chr(
        &dis_uuid.u, &model_uuid.u, &model_def_handle, &model_val_handle);
    int serial_rc = ble_gatts_find_chr(
        &dis_uuid.u, &serial_uuid.u, &serial_def_handle, &serial_val_handle);
    int firmware_rc = ble_gatts_find_chr(
        &dis_uuid.u, &firmware_uuid.u, &firmware_def_handle, &firmware_val_handle);
    int hardware_rc = ble_gatts_find_chr(
        &dis_uuid.u, &hardware_uuid.u, &hardware_def_handle, &hardware_val_handle);
    int software_rc = ble_gatts_find_chr(
        &dis_uuid.u, &software_uuid.u, &software_def_handle, &software_val_handle);
    int manufacturer_rc = ble_gatts_find_chr(
        &dis_uuid.u, &manufacturer_uuid.u, &manufacturer_def_handle, &manufacturer_val_handle);
    int pnp_rc = ble_gatts_find_chr(
        &dis_uuid.u, &pnp_uuid.u, &pnp_def_handle, &pnp_val_handle);

    ESP_LOGI(TAG,
             "DIS GATT state: svc_rc=%d svc_handle=%u "
             "model_rc=%d model_def=%u model_val=%u "
             "serial_rc=%d serial_def=%u serial_val=%u "
             "firmware_rc=%d firmware_def=%u firmware_val=%u "
             "hardware_rc=%d hardware_def=%u hardware_val=%u "
             "software_rc=%d software_def=%u software_val=%u "
             "manufacturer_rc=%d manufacturer_def=%u manufacturer_val=%u "
             "pnp_rc=%d pnp_def=%u pnp_val=%u",
             svc_rc,
             service_handle,
             model_rc,
             model_def_handle,
             model_val_handle,
             serial_rc,
             serial_def_handle,
             serial_val_handle,
             firmware_rc,
             firmware_def_handle,
             firmware_val_handle,
             hardware_rc,
             hardware_def_handle,
             hardware_val_handle,
             software_rc,
             software_def_handle,
             software_val_handle,
             manufacturer_rc,
             manufacturer_def_handle,
             manufacturer_val_handle,
             pnp_rc,
             pnp_def_handle,
             pnp_val_handle);
}

esp_err_t ble_hid_init(void)
{
    listener_device_set_safe_mode(s_safe_mode);
    uint32_t ready_mask = LISTENER_DEVICE_READY_DIAGNOSTIC;
    uint32_t degraded_mask = 0;
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
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(erase_ret));
            return erase_ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        ble_hid_publish_readiness(
            ready_mask,
            BLE_HID_READINESS_ALL & ~ready_mask,
            "nvs_init_failed");
        return ret;
    }

    s_ble_report_maps[0].data = hid_keyboard_get_report_map();
    s_ble_report_maps[0].len = hid_keyboard_get_report_map_size();

    if (s_ascii_queue == NULL) {
        s_ascii_queue = xQueueCreate(BLE_HID_ASCII_QUEUE_LENGTH, sizeof(char));
        if (s_ascii_queue == NULL) {
            ESP_LOGE(TAG, "ascii queue create failed");
            ble_hid_publish_readiness(
                ready_mask,
                BLE_HID_READINESS_ALL & ~ready_mask,
                "ascii_queue_failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_usage_queue == NULL) {
        s_usage_queue = xQueueCreate(BLE_HID_USAGE_QUEUE_LENGTH, sizeof(ble_hid_usage_event_t));
        if (s_usage_queue == NULL) {
            ESP_LOGE(TAG, "usage queue create failed");
            ble_hid_publish_readiness(
                ready_mask,
                BLE_HID_READINESS_ALL & ~ready_mask,
                "usage_queue_failed");
            return ESP_ERR_NO_MEM;
        }
    }

    ble_hid_gap_set_audio_enabled(!s_safe_mode);
    ret = ble_hid_gap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ble_hid_gap_init failed: %s", esp_err_to_name(ret));
        ble_hid_publish_readiness(
            ready_mask,
            BLE_HID_READINESS_ALL & ~ready_mask,
            "gap_init_failed");
        return ret;
    }

    if (s_safe_mode) {
        ESP_LOGW(TAG, "safe mode: BLE audio GATT disabled");
        degraded_mask |= LISTENER_DEVICE_READY_AUDIO;
    } else {
        ret = ble_audio_stream_init();
        if (ret == ESP_OK) {
            ret = ble_audio_stream_register_gatt();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "BLE audio GATT registration failed; HID/recovery continue: %s", esp_err_to_name(ret));
                degraded_mask |= LISTENER_DEVICE_READY_AUDIO;
            } else if (!audio_capture_is_available()) {
                const char *reason = audio_capture_get_unavailable_reason();
                ESP_LOGW(
                    TAG,
                    "BLE audio GATT registered but capture is degraded: %s",
                    reason != NULL ? reason : "audio_capture_unavailable");
                degraded_mask |= LISTENER_DEVICE_READY_AUDIO;
            } else {
                ready_mask |= LISTENER_DEVICE_READY_AUDIO;
            }
        } else {
            ESP_LOGW(TAG, "BLE audio init failed; HID/recovery continue without audio stream: %s", esp_err_to_name(ret));
            degraded_mask |= LISTENER_DEVICE_READY_AUDIO;
        }
    }

    ret = ble_firmware_ota_register_gatt();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE firmware OTA GATT registration failed; HID/audio continue: %s", esp_err_to_name(ret));
        degraded_mask |= LISTENER_DEVICE_READY_OTA;
    } else {
        ready_mask |= LISTENER_DEVICE_READY_OTA;
    }

    {
        int diag_rc = ble_diag_log_register_gatt();
        if (diag_rc != 0) {
            ESP_LOGW(TAG, "BLE diag log GATT registration failed: rc=%d", diag_rc);
        }
    }

    ret = ble_hid_gap_configure_advertising(ESP_HID_APPEARANCE_KEYBOARD, s_device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE advertising config failed: %s", esp_err_to_name(ret));
        ble_hid_publish_readiness(
            ready_mask,
            degraded_mask | (BLE_HID_READINESS_ALL & ~ready_mask),
            "advertising_config_failed");
        return ret;
    }

    ret = esp_hidd_dev_init(
        &s_ble_hid_config,
        ESP_HID_TRANSPORT_BLE,
        ble_hid_event_callback,
        &s_ble_hid_ctx.hid_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        ble_hid_publish_readiness(
            ready_mask,
            degraded_mask | LISTENER_DEVICE_READY_HID,
            "hid_init_failed");
        return ret;
    }
    ready_mask |= LISTENER_DEVICE_READY_HID;

    ble_hid_configure_dis_identity();
    ble_hid_log_dis_gatt_state();
    if (!s_safe_mode) {
        ble_audio_stream_log_gatt_state();
    }
    ble_firmware_ota_log_gatt_state();
    ble_diag_log_log_gatt_state();

    int gap_name_rc = ble_svc_gap_device_name_set(s_device_name);
    if (gap_name_rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", gap_name_rc);
    }

    ble_hid_update_battery_level("init", true);
    ble_hid_publish_readiness(ready_mask, degraded_mask, "init_complete");
    return ESP_OK;
}

void ble_hid_set_safe_mode(bool enabled)
{
    s_safe_mode = enabled;
}

esp_err_t ble_hid_start(void)
{
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    esp_err_t ret = esp_nimble_enable(ble_hid_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
    return ret;
}

bool ble_hid_is_connected(void)
{
    return s_ble_connected;
}

uint32_t ble_hid_get_disconnect_count(void)
{
    return ble_hid_disconnect_count_snapshot();
}
