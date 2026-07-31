#include "ble_hid.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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
#include "denzic_battery_v1.h"
#include "board.h"
#include "boot_safety.h"
#include "device_settings.h"
#include "ec11_rotation_control.h"
#include "listener_device.h"
#include "ble_hid_gap.h"
#include "ble_audio_stream.h"
#include "ble_firmware_ota.h"
#include "ble_diag_log.h"
#include "voice_recording_control.h"
#include "diag_log.h"
#include "firmware_ota.h"
#include "power_manager.h"
#include "status_led.h"
#include "watchdog_platform.h"
#include "esp_timer.h"

static const char *TAG = "ble_hid";

#define BLE_HID_BATTERY_FALLBACK_LEVEL 50
#define BLE_HID_BATTERY_SAMPLE_INTERVAL_MS 5000
#define BLE_HID_BATTERY_CONNECTED_IDLE_INTERVAL_MS 60000
#define BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS 600000
#define BLE_HID_BATTERY_FORCE_REFRESH_INTERVAL_MS DENZIC_BATTERY_V1_FORCE_REFRESH_INTERVAL_MS
#define BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT DENZIC_BATTERY_V1_NOTIFY_THRESHOLD_PERCENT
#define BLE_HID_BATTERY_LEVEL_INVALID DENZIC_BATTERY_V1_INVALID_LEVEL
#define BLE_HID_BATTERY_TASK_STACK_BYTES (4 * 1024)
#define BLE_HID_KEYBOARD_TASK_STACK_BYTES (5 * 1024)
#define BLE_HID_USB_COMMAND_PREFIX '~'
#define BLE_HID_USB_COMMAND_BUFFER_BYTES 192
#define BLE_HID_USB_READ_ACTIVE_TIMEOUT_MS 20
#define BLE_HID_USB_READ_LOW_POWER_TIMEOUT_MS 500
#define BLE_HID_ASCII_QUEUE_LENGTH 8
#define BLE_HID_USAGE_QUEUE_LENGTH 32
#define BLE_HID_KEY_SOURCE_BYTES 32
#define BLE_HID_PENDING_USAGE_TTL_MS 10000U
#define BLE_HID_PENDING_USAGE_POLL_MS 20U
#define BLE_HID_USAGE_TASK_STACK_BYTES (4 * 1024)
#define BLE_HID_USAGE_TASK_IDLE_POLL_MS 1000U
#define BLE_HID_READINESS_ALL \
    (LISTENER_DEVICE_READY_HID | LISTENER_DEVICE_READY_AUDIO | \
     LISTENER_DEVICE_READY_OTA | LISTENER_DEVICE_READY_DIAGNOSTIC)

typedef struct
{
    TaskHandle_t task_handle;
    TaskHandle_t usage_task_handle;
    TaskHandle_t battery_task_handle;
    esp_hidd_dev_t *hid_device;
} ble_hid_ctx_t;

typedef struct {
    uint16_t usage;
    uint8_t modifier;
    bool consumer;
    TickType_t queued_tick;
    char source[BLE_HID_KEY_SOURCE_BYTES];
} ble_hid_usage_event_t;

static ble_hid_ctx_t s_ble_hid_ctx = {0};

static const char *s_device_name = DEVICE_SETTINGS_DEFAULT_BLE_NAME;

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
static ble_hid_usb_command_handler_t s_usb_command_handler;

static bool s_ble_connected;
static bool s_hid_control_suspended;
static uint32_t s_disconnect_count;
static portMUX_TYPE s_disconnect_count_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_connect_timestamp_ms;
static uint32_t s_battery_forced_refresh_timestamp_ms;
static QueueHandle_t s_ascii_queue;
static QueueHandle_t s_usage_queue;
static SemaphoreHandle_t s_usage_drain_mutex;
static bool s_safe_mode;
static bool s_usage_transport_test_blocked;
static bool s_battery_service_valid;
static uint8_t s_battery_service_level = BLE_HID_BATTERY_LEVEL_INVALID;
static denzic_battery_v1_charge_tracker_t s_battery_charge_tracker;

static void ble_hid_log_dis_gatt_state(void);
static void ble_hid_usage_task_start(void);

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

static status_led_ble_state_t ble_hid_connected_status_led_state(void)
{
    return ble_audio_stream_is_type_led_ready()
        ? STATUS_LED_BLE_TYPE_READY
        : STATUS_LED_BLE_CONNECTED;
}

static void ble_hid_resync_connected_status_led(void)
{
    if (!s_ble_connected || !ble_hid_gap_is_securely_connected()) {
        return;
    }
    if (ble_hid_gap_is_recovery_pairing_window_open()) {
        return;
    }
    status_led_set_ble_state(ble_hid_connected_status_led_state(), false);
}

static uint32_t ble_hid_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static bool ble_hid_low_power_idle_active(void)
{
    power_manager_state_t state = power_manager_get_state();
    return state == POWER_MANAGER_STATE_CONNECTED_IDLE ||
           state == POWER_MANAGER_STATE_DISCONNECTED_IDLE ||
           state == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN;
}

static uint32_t ble_hid_battery_sample_interval_ms(bool low_power_idle)
{
    if (!s_ble_connected) {
        return BLE_HID_BATTERY_DISCONNECTED_IDLE_INTERVAL_MS;
    }
    return low_power_idle
        ? BLE_HID_BATTERY_CONNECTED_IDLE_INTERVAL_MS
        : BLE_HID_BATTERY_SAMPLE_INTERVAL_MS;
}

static const char *ble_hid_battery_sample_reason(bool low_power_idle)
{
    if (!s_ble_connected) {
        return "idle_sample";
    }
    return low_power_idle ? "connected_idle_sample" : "threshold_sample";
}

void ble_hid_battery_task_wake(void)
{
    if (s_ble_hid_ctx.battery_task_handle != NULL) {
        xTaskNotifyGive(s_ble_hid_ctx.battery_task_handle);
    }
}

static esp_err_t ble_hid_update_battery_level(const char *reason, bool force_notify)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ble_connected && !force_notify) {
        ESP_LOGD(TAG, "battery update skipped while disconnected reason=%s",
                 reason != NULL ? reason : "unspecified");
        return ESP_ERR_INVALID_STATE;
    }

    battery_monitor_status_t battery = {0};
    esp_err_t read_ret = battery_monitor_read(&battery);
    bool battery_valid = read_ret == ESP_OK && battery.valid;
    uint8_t level = battery_valid ? battery.level_percent : BLE_HID_BATTERY_FALLBACK_LEVEL;
    uint32_t battery_mv = battery_valid ? battery.voltage_mv : 0U;
    board_v2_power_input_snapshot_t power = {0};
    board_get_v2_power_input_snapshot(&power);
    bool usb_power_present = power.usb_power_present;
    bool charger_active = power.bat_chg_level == 0;
    bool charge_power_present = usb_power_present || charger_active;
    bool raw_charging = charger_active;
    bool raw_full = power.bat_std_level == 0;
    uint32_t now_ms = ble_hid_now_ms();
    denzic_battery_v1_charge_input_t charge_input = {
        .charge_power_present = charge_power_present,
        .battery_valid = battery_valid,
        .level_percent = level,
        .battery_mv = battery_mv,
        .raw_charging = raw_charging,
        .raw_full = raw_full,
        .now_ms = now_ms,
    };
    denzic_battery_v1_charge_decision_t charge =
        denzic_battery_v1_update_charge(&s_battery_charge_tracker, &charge_input);
    bool charge_full = charge.state == DENZIC_BATTERY_V1_CHARGE_STATE_FULL;
    charge_power_present = charge.charge_power_present;
    level = charge.published_level;
    uint32_t full_candidate_ms = charge.full_candidate_ms;
    bool periodic_refresh =
        now_ms - s_battery_forced_refresh_timestamp_ms >=
        BLE_HID_BATTERY_FORCE_REFRESH_INTERVAL_MS;
    denzic_battery_v1_notify_input_t notify_input = {
        .previous_valid = battery_valid ? s_battery_service_valid : true,
        .previous_level = battery_valid ? s_battery_service_level : level,
        .level = level,
        .force = force_notify,
        .now_ms = now_ms,
        .last_notify_ms = s_battery_forced_refresh_timestamp_ms,
        .periodic_interval_ms = BLE_HID_BATTERY_FORCE_REFRESH_INTERVAL_MS,
        .threshold_percent = BLE_HID_BATTERY_NOTIFY_THRESHOLD_PERCENT,
    };
    denzic_battery_v1_notify_decision_t notify =
        denzic_battery_v1_decide_notify(&notify_input);
    bool should_notify = notify.notify;

    firmware_ota_note_battery(
        level,
        (read_ret == ESP_OK && battery.valid) ? battery.voltage_mv : 0,
        read_ret == ESP_OK && battery.valid);

    ble_hid_resync_connected_status_led();

    if (!should_notify) {
        ESP_LOGD(
            TAG,
            "battery unchanged level=%u last=%u reason=%s",
            level,
            s_battery_service_level,
            reason != NULL ? reason : "unspecified");
        return ESP_OK;
    }

    if (read_ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "battery notify level=%u voltage_mv=%" PRIu32
            " raw_adc=%d adc_mv=%d adc_driver_mv=%d adc_raw_mv=%d"
            " adc_trim_mv=%d adc_correction_mv=%d adc_trim_valid=%u adc_trim_result=%s"
            " usb_power=%u charger_active=%u charge_power=%u"
            " raw_charging=%u raw_full=%u full_latched=%u full_candidate_ms=%u charge_full=%u"
            " usb_det_adc_valid=%u usb_det_adc_mv=%d usb_det_mismatch=%u"
            " reason=%s forced=%u periodic=%u",
            level,
            battery.voltage_mv,
            battery.raw_adc,
            battery.adc_mv,
            battery.adc_driver_mv,
            battery.adc_raw_mv,
            battery.adc_trim_mv,
            battery.adc_correction_mv,
            battery.adc_trim_valid ? 1u : 0u,
            esp_err_to_name(battery.adc_trim_result),
            usb_power_present ? 1u : 0u,
            charger_active ? 1u : 0u,
            charge_power_present ? 1u : 0u,
            raw_charging ? 1u : 0u,
            raw_full ? 1u : 0u,
            charge.full_latched ? 1u : 0u,
            (unsigned)full_candidate_ms,
            charge_full ? 1u : 0u,
            power.usb_det_adc_valid ? 1u : 0u,
            power.usb_det_adc_mv,
            power.usb_det_mismatch ? 1u : 0u,
            reason != NULL ? reason : "unspecified",
            force_notify ? 1u : 0u,
            periodic_refresh ? 1u : 0u);
    } else {
        ESP_LOGW(
            TAG,
            "battery notify fallback=%u usb_power=%u charger_active=%u charge_power=%u"
            " raw_charging=%u raw_full=%u full_latched=%u full_candidate_ms=%u charge_full=%u"
            " usb_det_adc_valid=%u usb_det_adc_mv=%d usb_det_mismatch=%u"
            " reason=%s read_failed=%s forced=%u periodic=%u",
            level,
            usb_power_present ? 1u : 0u,
            charger_active ? 1u : 0u,
            charge_power_present ? 1u : 0u,
            raw_charging ? 1u : 0u,
            raw_full ? 1u : 0u,
            charge.full_latched ? 1u : 0u,
            (unsigned)full_candidate_ms,
            charge_full ? 1u : 0u,
            power.usb_det_adc_valid ? 1u : 0u,
            power.usb_det_adc_mv,
            power.usb_det_mismatch ? 1u : 0u,
            reason != NULL ? reason : "unspecified",
            esp_err_to_name(read_ret),
            force_notify ? 1u : 0u,
            periodic_refresh ? 1u : 0u);
    }

    esp_err_t ret = esp_hidd_dev_battery_set(s_ble_hid_ctx.hid_device, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery level notify failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_battery_service_level = level;
    s_battery_service_valid = true;
    if (force_notify || periodic_refresh) {
        s_battery_forced_refresh_timestamp_ms = now_ms;
    }

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

    return ESP_OK;
}

esp_err_t ble_hid_battery_force_refresh(const char *reason)
{
    if (!s_ble_connected) {
        ESP_LOGI(TAG, "battery force refresh skipped while disconnected reason=%s",
                 reason != NULL ? reason : "unspecified");
        return ESP_ERR_INVALID_STATE;
    }

    return ble_hid_update_battery_level(reason != NULL ? reason : "force_refresh", true);
}

static void ble_hid_battery_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("ble_hid_battery_task");

    while (1) {
        bool low_power_idle = ble_hid_low_power_idle_active();
        uint32_t wait_ms = ble_audio_stream_type_link_poll_wait_ms(
            ble_hid_battery_sample_interval_ms(low_power_idle));
        uint32_t notified = 0;
        if (s_ble_connected && !low_power_idle) {
            notified = watchdog_platform_task_notify_take(
                pdTRUE,
                wait_ms);
        } else {
            notified = watchdog_platform_task_notify_take_low_power(
                pdTRUE,
                wait_ms);
        }

        low_power_idle = ble_hid_low_power_idle_active();
        if (notified != 0 && low_power_idle) {
            ble_audio_stream_poll_type_link();
            watchdog_platform_feed_current_task();
            continue;
        }

        ble_hid_update_battery_level(ble_hid_battery_sample_reason(low_power_idle), false);
        ble_audio_stream_poll_type_link();
        watchdog_platform_feed_current_task();
    }
}

/*
 * HID 任务栈策略（PSRAM 迁移后的铁律）：
 * - battery / usage：低频，不碰 flash/NVS/OTA → 可放 PSRAM 省片内 DRAM
 * - keyboard：处理 USB 串口命令（含 ~OTA:STATUS / DEVICE:SET），会调用
 *   esp_ota_get_state_partition / nvs_* 等关 cache 的 API。栈必须在片内 BSS。
 *   实测：PSRAM 栈上跑 OTA 防回退查询 →
 *   s_task_stack_is_sane_when_cache_frozen assert → 重启灯循环。
 */
static StaticTask_t s_ble_hid_keyboard_task_control;
static StackType_t s_ble_hid_keyboard_task_stack[BLE_HID_KEYBOARD_TASK_STACK_BYTES];
static StaticTask_t s_ble_hid_battery_task_control;
static StackType_t *s_ble_hid_battery_task_stack;
static StaticTask_t s_ble_hid_usage_task_control;
static StackType_t *s_ble_hid_usage_task_stack;

static void ble_hid_battery_task_start(void)
{
    if (s_ble_hid_ctx.battery_task_handle != NULL) {
        return;
    }

    BaseType_t task_ok = watchdog_platform_start_task_on_spiram(
        ble_hid_battery_task,
        "ble_hid_battery_task",
        BLE_HID_BATTERY_TASK_STACK_BYTES,
        configMAX_PRIORITIES - 5,
        &s_ble_hid_ctx.battery_task_handle,
        &s_ble_hid_battery_task_control,
        &s_ble_hid_battery_task_stack);
    if (task_ok != pdPASS) {
        s_ble_hid_ctx.battery_task_handle = NULL;
        ESP_LOGW(TAG,
                 "battery update task create failed spiram_heap_min=%u",
                 (unsigned)esp_get_minimum_free_heap_size());
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

static esp_err_t ble_hid_dispatch_usage(uint8_t usage, uint8_t modifier, const char *source)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID device unavailable", source);
        return ESP_ERR_INVALID_STATE;
    }

    if (!esp_hidd_dev_connected(s_ble_hid_ctx.hid_device)) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID host not connected", source);
        return ESP_ERR_INVALID_STATE;
    }

    return hid_keyboard_send_usage_with_modifier(usage, modifier, s_ble_hid_ctx.hid_device);
}

static esp_err_t ble_hid_dispatch_consumer_usage(uint16_t usage, const char *source)
{
    if (s_ble_hid_ctx.hid_device == NULL) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID device unavailable", source);
        return ESP_ERR_INVALID_STATE;
    }

    if (!esp_hidd_dev_connected(s_ble_hid_ctx.hid_device)) {
        ESP_LOGW(TAG, "%s dispatch dropped: HID host not connected", source);
        return ESP_ERR_INVALID_STATE;
    }

    return hid_keyboard_send_consumer_usage(usage, s_ble_hid_ctx.hid_device);
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

static bool ble_hid_usage_transport_ready(void)
{
    return !s_usage_transport_test_blocked &&
           s_ble_connected &&
           !s_hid_control_suspended &&
           s_ble_hid_ctx.hid_device != NULL &&
           esp_hidd_dev_connected(s_ble_hid_ctx.hid_device);
}

static uint32_t ble_hid_usage_event_age_ms(const ble_hid_usage_event_t *event, TickType_t now)
{
    if (event == NULL) {
        return UINT32_MAX;
    }
    uint64_t elapsed_ms = (uint64_t)(now - event->queued_tick) * (uint64_t)portTICK_PERIOD_MS;
    return elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
}

static bool ble_hid_usage_event_expired(const ble_hid_usage_event_t *event, TickType_t now)
{
    return ble_hid_usage_event_age_ms(event, now) > BLE_HID_PENDING_USAGE_TTL_MS;
}

static bool ble_hid_usage_queue_has_pending(void)
{
    return s_usage_queue != NULL && uxQueueMessagesWaiting(s_usage_queue) > 0;
}

static bool ble_hid_usage_dispatch_should_retry(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL || ret == ESP_ERR_TIMEOUT;
}

static void ble_hid_request_usage_reconnect(const char *reason)
{
    power_manager_record_activity(reason != NULL ? reason : "hid_usage_wake");
    (void)ble_hid_gap_request_reconnect();
}

static void ble_hid_signal_usage_task(void)
{
    if (s_ble_hid_ctx.usage_task_handle != NULL) {
        xTaskNotifyGive(s_ble_hid_ctx.usage_task_handle);
    }
}

static esp_err_t ble_hid_enqueue_usage_event(ble_hid_usage_event_t *event)
{
    if (event == NULL || s_usage_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    event->queued_tick = xTaskGetTickCount();
    if (xQueueSend(s_usage_queue, event, 0) != pdTRUE) {
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_QUEUE_DROP, DIAG_SEV_WARN,
                 event->usage, BLE_HID_USAGE_QUEUE_LENGTH, event->modifier, event->consumer ? 1u : 0u);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void ble_hid_drain_usage_queue(void)
{
    if (s_usage_queue == NULL) {
        return;
    }
    if (!ble_hid_usage_transport_ready()) {
        return;
    }

    bool lock_taken = false;
    if (s_usage_drain_mutex != NULL) {
        if (xSemaphoreTake(s_usage_drain_mutex, 0) != pdTRUE) {
            return;
        }
        lock_taken = true;
    }

    ble_hid_usage_event_t event;
    while (xQueueReceive(s_usage_queue, &event, 0) == pdTRUE) {
        const char *source = event.source[0] != '\0' ? event.source : "CUSTOM_KEY";
        TickType_t now = xTaskGetTickCount();
        if (ble_hid_usage_event_expired(&event, now)) {
            uint32_t age_ms = ble_hid_usage_event_age_ms(&event, now);
            ESP_LOGW(
                TAG,
                "%s pending HID usage expired: usage=0x%04X modifier=0x%02X consumer=%u age_ms=%" PRIu32,
                source,
                event.usage,
                event.modifier,
                event.consumer ? 1u : 0u,
                age_ms);
            diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_QUEUE_DROP, DIAG_SEV_WARN,
                     event.usage, BLE_HID_PENDING_USAGE_TTL_MS, event.modifier, event.consumer ? 1u : 0u);
            continue;
        }

        esp_err_t ret = event.consumer
            ? ble_hid_dispatch_consumer_usage(event.usage, source)
            : ble_hid_dispatch_usage((uint8_t)event.usage, event.modifier, source);
        if (ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "%s dispatch failed: usage=0x%04X modifier=0x%02X consumer=%u error=%s",
                source,
                event.usage,
                event.modifier,
                event.consumer ? 1u : 0u,
                esp_err_to_name(ret));
            if (ble_hid_usage_dispatch_should_retry(ret)) {
                if (xQueueSendToFront(s_usage_queue, &event, 0) != pdTRUE) {
                    diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_QUEUE_DROP, DIAG_SEV_WARN,
                             event.usage, BLE_HID_USAGE_QUEUE_LENGTH, event.modifier, event.consumer ? 1u : 0u);
                }
                ble_hid_request_usage_reconnect("hid_usage_retry");
                break;
            }
        }
    }

    if (lock_taken) {
        xSemaphoreGive(s_usage_drain_mutex);
    }
}

static void ble_hid_usage_task(void *parameter)
{
    (void)parameter;

    while (1) {
        ble_hid_drain_usage_queue();
        TickType_t wait_ticks = ble_hid_usage_queue_has_pending()
            ? pdMS_TO_TICKS(BLE_HID_PENDING_USAGE_POLL_MS)
            : pdMS_TO_TICKS(BLE_HID_USAGE_TASK_IDLE_POLL_MS);
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}

static void ble_hid_usage_task_start(void)
{
    if (s_ble_hid_ctx.usage_task_handle != NULL) {
        return;
    }

    BaseType_t task_ok = watchdog_platform_start_task_on_spiram(
        ble_hid_usage_task,
        "ble_hid_usage_task",
        BLE_HID_USAGE_TASK_STACK_BYTES,
        configMAX_PRIORITIES - 3,
        &s_ble_hid_ctx.usage_task_handle,
        &s_ble_hid_usage_task_control,
        &s_ble_hid_usage_task_stack);
    if (task_ok != pdPASS) {
        s_ble_hid_ctx.usage_task_handle = NULL;
        ESP_LOGE(TAG,
                 "failed to start BLE HID usage task spiram_heap_min=%u",
                 (unsigned)esp_get_minimum_free_heap_size());
    }
}

esp_err_t ble_hid_send_ascii_async(char input_char)
{
    if (s_ascii_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ble_hid_is_connected()) {
        power_manager_record_activity("hid_key_wake");
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
    return ble_hid_send_keyboard_usage_with_modifier_async(usage, 0, source);
}

esp_err_t ble_hid_send_keyboard_usage_with_modifier_async(uint8_t usage, uint8_t modifier, const char *source)
{
    if (s_usage_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *event_source = source != NULL ? source : "CUSTOM_KEY";
    ble_hid_usage_event_t event = {
        .usage = usage,
        .modifier = modifier,
        .consumer = false,
    };
    if (source != NULL) {
        snprintf(event.source, sizeof(event.source), "%s", source);
    }

    esp_err_t ret = ble_hid_enqueue_usage_event(&event);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!ble_hid_usage_transport_ready()) {
        ble_hid_request_usage_reconnect("hid_usage_wake");
        ESP_LOGI(
            TAG,
            "%s pending HID usage queued for reconnect: usage=0x%02X modifier=0x%02X depth=%u ttl_ms=%u",
            event_source,
            usage,
            modifier,
            (unsigned)uxQueueMessagesWaiting(s_usage_queue),
            (unsigned)BLE_HID_PENDING_USAGE_TTL_MS);
        return ESP_OK;
    }

    power_manager_record_activity("hid_usage_enqueue");
    ESP_LOGI(
        TAG,
        "%s HID usage queued: usage=0x%02X modifier=0x%02X depth=%u",
        event_source,
        usage,
        modifier,
        (unsigned)uxQueueMessagesWaiting(s_usage_queue));
    ble_hid_signal_usage_task();
    return ESP_OK;
}

esp_err_t ble_hid_send_keyboard_usage_pending_test_async(uint8_t usage, uint8_t modifier, const char *source)
{
    bool previous_blocked = s_usage_transport_test_blocked;
    s_usage_transport_test_blocked = true;
    esp_err_t ret = ble_hid_send_keyboard_usage_with_modifier_async(usage, modifier, source);
    s_usage_transport_test_blocked = previous_blocked;

    if (ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "%s pending HID usage self-test releasing transport block: usage=0x%02X modifier=0x%02X depth=%u",
            source != NULL ? source : "CUSTOM_KEY",
            usage,
            modifier,
            s_usage_queue != NULL ? (unsigned)uxQueueMessagesWaiting(s_usage_queue) : 0u);
        ble_hid_drain_usage_queue();
    }
    return ret;
}

esp_err_t ble_hid_send_consumer_usage_async(uint16_t usage, const char *source)
{
    if (s_usage_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_hid_usage_event_t event = {
        .usage = usage,
        .consumer = true,
    };
    if (source != NULL) {
        snprintf(event.source, sizeof(event.source), "%s", source);
    }

    esp_err_t ret = ble_hid_enqueue_usage_event(&event);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!ble_hid_usage_transport_ready()) {
        ble_hid_request_usage_reconnect("hid_consumer_wake");
        ESP_LOGI(
            TAG,
            "%s pending consumer usage queued for reconnect: usage=0x%04X depth=%u ttl_ms=%u",
            source != NULL ? source : "CONSUMER",
            usage,
            (unsigned)uxQueueMessagesWaiting(s_usage_queue),
            (unsigned)BLE_HID_PENDING_USAGE_TTL_MS);
        return ESP_OK;
    }

    power_manager_record_activity("hid_consumer_enqueue");
    ble_hid_signal_usage_task();
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

static bool ble_hid_usb_command_is_passive_query(const char *line)
{
    if (line == NULL) {
        return false;
    }
    if (line[0] == '~') {
        line++;
    }

    return strcmp(line, "POWER:STATUS") == 0 ||
           strcmp(line, "POWER:IDLE") == 0 ||
           strcmp(line, "POWER:IDLE:DIAG") == 0 ||
           strcmp(line, "POWER:IDLE_DIAG") == 0 ||
           strcmp(line, "POWER:PM") == 0 ||
           strcmp(line, "POWER:PM:LOCKS") == 0 ||
           strcmp(line, "BOARD:STATUS") == 0 ||
           strcmp(line, "BOARD:POWER") == 0 ||
           strcmp(line, "BOARD:POWER:FORCE") == 0 ||
           strcmp(line, "BATTERY:STATUS") == 0 ||
           strcmp(line, "LED:STATUS") == 0 ||
           strcmp(line, "LED:BUDGET") == 0 ||
           strcmp(line, "LED:PRIVACY") == 0 ||
           strcmp(line, "BLE:STATUS") == 0 ||
           strcmp(line, "DEVICE:SETTINGS") == 0 ||
           strcmp(line, "DEVICE:STATUS") == 0 ||
           strcmp(line, "OTA:STATUS") == 0 ||
           strcmp(line, "OTA:BLOCKER") == 0;
}

static bool ble_hid_usb_command_matches(const char *line, const char *command)
{
    if (line == NULL || command == NULL) {
        return false;
    }
    if (line[0] == '~') {
        line++;
    }
    return strcmp(line, command) == 0;
}

static bool ble_hid_usb_command_starts_with(const char *line, const char *prefix)
{
    if (line == NULL || prefix == NULL) {
        return false;
    }
    if (line[0] == '~') {
        line++;
    }
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static bool ble_hid_usb_command_starts_with_boundary(const char *line, const char *prefix)
{
    if (!ble_hid_usb_command_starts_with(line, prefix)) {
        return false;
    }
    if (line[0] == '~') {
        line++;
    }
    char next = line[strlen(prefix)];
    return next == '\0' || next == ' ' || next == ':' || next == '\r' || next == '\n';
}

static bool ble_hid_usb_command_is_apply_ble_name(const char *line)
{
    return ble_hid_usb_command_matches(line, "DEVICE:APPLY_BLE_NAME") ||
           ble_hid_usb_command_matches(line, "DEVICE:BLE_NAME:APPLY");
}

static bool ble_hid_usb_command_is_fast_idle_diagnostic_key(const char *line)
{
    return ble_hid_usb_command_starts_with_boundary(line, "KEY:EC11:SINGLE");
}

static bool ble_hid_usb_command_records_activity(const char *line)
{
    /*
     * The generated EC11 single click is a diagnostic stand-in for the first
     * physical edge. Waking the power manager before it reaches the input
     * state machine would make CONNECTED_IDLE unavailable and turn the test
     * into the delayed HID fallback instead of exercising fast recording.
     */
    if (ble_hid_usb_command_is_passive_query(line) ||
        ble_hid_usb_command_is_fast_idle_diagnostic_key(line)) {
        return false;
    }

    return
        ble_hid_usb_command_matches(line, "POWER:SHUTDOWN") ||
        ble_hid_usb_command_matches(line, "POWER:ACTIVITY") ||
        ble_hid_usb_command_matches(line, "LED:OFF") ||
        ble_hid_usb_command_matches(line, "LED:WAKE") ||
        ble_hid_usb_command_starts_with(line, "LED:BRIGHTNESS ") ||
        ble_hid_usb_command_starts_with(line, "LED:PROFILE ") ||
        ble_hid_usb_command_starts_with_boundary(line, "LED:TEST:RGBW") ||
        ble_hid_usb_command_starts_with_boundary(line, "LED:TEST:MAP") ||
        ble_hid_usb_command_starts_with_boundary(line, "LED:CHASE") ||
        ble_hid_usb_command_starts_with(line, "LED:TEST:PIXEL ") ||
        ble_hid_usb_command_starts_with(line, "LED:PREVIEW ") ||
        ble_hid_usb_command_starts_with(line, "LED:ERROR ") ||
        ble_hid_usb_command_starts_with(line, "DEVICE:SET ") ||
        ble_hid_usb_command_is_apply_ble_name(line) ||
        ble_hid_usb_command_matches(line, "DEVICE:RESET") ||
        ble_hid_usb_command_matches(line, "BOOT:CLEAR") ||
        ble_hid_usb_command_matches(line, "BOOT:CRASH") ||
        ble_hid_usb_command_matches(line, "WDT:DEADLOCK") ||
        ble_hid_usb_command_matches(line, "OTA:ABORT") ||
        ble_hid_usb_command_matches(line, "OTA:TEST_BOOT_INACTIVE") ||
        ble_hid_usb_command_matches(line, "DIAGLOG:CLEAR") ||
        ble_hid_usb_command_matches(line, "DIAGLOG:INPUTDBG:ON") ||
        ble_hid_usb_command_matches(line, "DIAGLOG:INPUTDBG:OFF") ||
        ble_hid_usb_command_starts_with_boundary(line, "DIAGLOG:ENABLE") ||
        ble_hid_usb_command_starts_with_boundary(line, "DIAGLOG:DISABLE") ||
        ble_hid_usb_command_starts_with_boundary(line, "KEY") ||
        ble_hid_usb_command_matches(line, "KEY:KEY3:SINGLE") ||
        ble_hid_usb_command_matches(line, "KEY:3:SINGLE") ||
        ble_hid_usb_command_matches(line, "KEY:EC11:SINGLE") ||
        ble_hid_usb_command_matches(line, "KEY:VOICE:SINGLE") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:VOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:SYSTEM_VOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:SYSTEMVOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:BRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:SCREEN_BRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:SCREENBRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:DISABLED") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:DISABLE") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:OFF") ||
        ble_hid_usb_command_matches(line, "EC11:MODE:NONE") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:VOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:SYSTEM_VOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:SYSTEMVOLUME") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:BRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:SCREEN_BRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:SCREENBRIGHTNESS") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:DISABLED") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:DISABLE") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:OFF") ||
        ble_hid_usb_command_matches(line, "EC11:ACTION:NONE") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:CW") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:UP") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:INCREASE") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:RIGHT") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:CCW") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:DOWN") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:DECREASE") ||
        ble_hid_usb_command_matches(line, "EC11:ROTATE:LEFT") ||
        ble_hid_usb_command_matches(line, "VREC:TOGGLE") ||
        ble_hid_usb_command_matches(line, "VREC:CANCEL") ||
        ble_hid_usb_command_matches(line, "VREC:ACTIVATE") ||
        /* VREC:STOP/CLEANUP/PROCESSING* must NOT record activity: Type sends
         * STOP after ambient wake rejects and would reset the 5-minute
         * low-power idle clock (owner: 两次都只能进一次). */
        ble_hid_usb_command_matches(line, "VREC:RECOVERY:TYPE:MANUAL") ||
        ble_hid_usb_command_matches(line, "VREC:RECOVERY:TYPE") ||
        ble_hid_usb_command_matches(line, "VREC:RECOVERY_TYPE") ||
        ble_hid_usb_command_matches(line, "VREC:RECOVERY") ||
        ble_hid_usb_command_matches(line, "VREC:RESET") ||
        ble_hid_usb_command_matches(line, "VREC:FORGET");
}

static bool ble_hid_dispatch_usb_command_line(const char *line)
{
    if (ble_hid_usb_command_records_activity(line)) {
        power_manager_record_activity("usb_control_line");
    } else if (ble_hid_usb_command_is_fast_idle_diagnostic_key(line)) {
        ESP_LOGI(TAG, "EC11 generated single preserves CONNECTED_IDLE before raw edge");
    }

    if (power_manager_consume_usb_command(line)) {
        return true;
    }

    if (ble_hid_usb_command_is_apply_ble_name(line)) {
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, true);
        esp_err_t apply_ret = ble_hid_gap_apply_pending_ble_name();
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
        if (apply_ret == ESP_OK) {
            status_led_apply_device_settings();
            device_settings_consume_usb_command("~DEVICE:SETTINGS");
        } else {
            printf("~DEVICE:ERROR key=APPLY_BLE_NAME reason=%s\n", esp_err_to_name(apply_ret));
        }
        return true;
    }

    if (device_settings_consume_usb_command(line)) {
        status_led_apply_device_settings();
        return true;
    }

    if (status_led_consume_usb_command(line)) {
        return true;
    }

    if (board_consume_usb_command(line)) {
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

    if (strcmp(line, "~BLE:STATUS") == 0 || strcmp(line, "BLE:STATUS") == 0) {
        ble_hid_gap_print_status();
        return true;
    }

    if (strncmp(line, "~OTA:", strlen("~OTA:")) == 0) {
        if (ble_hid_usb_command_is_passive_query(line)) {
            return firmware_ota_consume_usb_command(line);
        }
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

    if (s_usb_command_handler != NULL) {
        esp_err_t handler_ret = ESP_ERR_NOT_FOUND;
        if (s_usb_command_handler(line, &handler_ret)) {
            if (handler_ret != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "USB extension command failed: line=%s error=%s",
                    line,
                    esp_err_to_name(handler_ret));
            }
            return true;
        }
    }

    esp_err_t ec11_ret = ESP_OK;
    if (ec11_rotation_control_consume_command(line, "usb", &ec11_ret)) {
        if (ec11_ret != ESP_OK) {
            ESP_LOGW(TAG, "EC11 USB control command failed: %s", esp_err_to_name(ec11_ret));
        }
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

        uint32_t usb_read_timeout_ms = ble_hid_low_power_idle_active()
            ? BLE_HID_USB_READ_LOW_POWER_TIMEOUT_MS
            : BLE_HID_USB_READ_ACTIVE_TIMEOUT_MS;
        if (ble_hid_usage_queue_has_pending() &&
            usb_read_timeout_ms > BLE_HID_PENDING_USAGE_POLL_MS) {
            usb_read_timeout_ms = BLE_HID_PENDING_USAGE_POLL_MS;
        }
        int bytes_read = usb_serial_jtag_read_bytes(
            rx_buffer,
            sizeof(rx_buffer),
            pdMS_TO_TICKS(usb_read_timeout_ms));
        watchdog_platform_feed_current_task();
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

    /* 片内静态栈：USB/OTA 防回退查询与 DEVICE 设置路径会写/读 flash。 */
    s_ble_hid_ctx.task_handle = xTaskCreateStatic(
        ble_hid_keyboard_task,
        "ble_hid_keyboard_task",
        BLE_HID_KEYBOARD_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 3,
        s_ble_hid_keyboard_task_stack,
        &s_ble_hid_keyboard_task_control);
    if (s_ble_hid_ctx.task_handle == NULL) {
        ESP_LOGE(TAG,
                 "failed to start BLE HID keyboard task on internal static stack "
                 "internal_free=%u heap_min=%u",
                 (unsigned)esp_get_free_internal_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
        return;
    }
    ESP_LOGI(TAG,
             "keyboard task started stack_heap=internal_static words=%u",
             (unsigned)BLE_HID_KEYBOARD_TASK_STACK_BYTES);
}

void ble_hid_task_start_up(void)
{
    ble_hid_task_start();
    ble_hid_usage_task_start();
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
        ble_hid_usage_task_start();
        ble_hid_signal_usage_task();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "CONNECT");
        s_ble_connected = true;
        s_hid_control_suspended = false;
        ble_hid_battery_task_wake();
        power_manager_set_ble_connected(true);
        s_connect_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        ble_hid_update_battery_level("connect_restore", true);
        uint32_t disconnect_count = ble_hid_disconnect_count_snapshot();
        diag_log(DIAG_SRC_BLE_HID, DIAG_BLE_CONNECT, DIAG_SEV_INFO,
                 1, esp_get_free_heap_size() / 1024, disconnect_count, 0);
        ble_hid_signal_usage_task();
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
        s_hid_control_suspended = !param->control.control;
        if (param->control.control) {
            ble_hid_task_start();
            ble_hid_usage_task_start();
            ble_hid_signal_usage_task();
        } else {
            ESP_LOGI(TAG, "HID suspended; keeping USB serial command task active");
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
            s_hid_control_suspended = false;
            ble_hid_battery_task_wake();
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

/*
 * esp_nimble_enable() uses xTaskCreatePinnedToCore and:
 *  1) ignores create failure (still returns ESP_OK)
 *  2) with CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY may put the host stack
 *     in PSRAM. NimBLE host on PSRAM stack never reaches sync → no advertising
 *     → ble=OFF → EC11 re-pair / Type pairing fail.
 * Pin a static INTERNAL DRAM stack instead (same depth as Kconfig words).
 */
#ifndef CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 8192
#endif
#define BLE_HID_NIMBLE_HOST_STACK_WORDS ((uint32_t)CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE)
static StaticTask_t s_nimble_host_tcb;
static StackType_t s_nimble_host_stack[BLE_HID_NIMBLE_HOST_STACK_WORDS];
static TaskHandle_t s_nimble_host_task_handle = NULL;

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
    s_device_name = listener_device_get_ble_name();
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

    if (s_usage_drain_mutex == NULL) {
        s_usage_drain_mutex = xSemaphoreCreateMutex();
        if (s_usage_drain_mutex == NULL) {
            ESP_LOGE(TAG, "usage drain mutex create failed");
            ble_hid_publish_readiness(
                ready_mask,
                BLE_HID_READINESS_ALL & ~ready_mask,
                "usage_drain_mutex_failed");
            return ESP_ERR_NO_MEM;
        }
    }
    ble_hid_usage_task_start();

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

    s_ble_hid_config.device_name = s_device_name;
    int gap_name_rc = ble_svc_gap_device_name_set(s_device_name);
    if (gap_name_rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", gap_name_rc);
    }
    ESP_LOGI(TAG, "BLE device name configured: gap/hid/advertising=%s", s_device_name);

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

    if (s_nimble_host_task_handle != NULL) {
        ESP_LOGW(TAG, "ble_hid_start: NimBLE host already running");
        return ESP_OK;
    }

    const size_t free_internal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(
        TAG,
        "ble_hid_start: NimBLE host internal static stack words=%u (~%u KB) internal_free=%u",
        (unsigned)BLE_HID_NIMBLE_HOST_STACK_WORDS,
        (unsigned)((BLE_HID_NIMBLE_HOST_STACK_WORDS * sizeof(StackType_t)) / 1024U),
        (unsigned)free_internal);

    /* Priority matches esp_nimble_enable (configMAX_PRIORITIES - 4); core 0 is
     * the IDF default NIMBLE_CORE when not pinned elsewhere. */
    s_nimble_host_task_handle = xTaskCreateStaticPinnedToCore(
        ble_hid_host_task,
        "nimble_host",
        BLE_HID_NIMBLE_HOST_STACK_WORDS,
        NULL,
        (configMAX_PRIORITIES - 4),
        s_nimble_host_stack,
        &s_nimble_host_tcb,
        0);
    if (s_nimble_host_task_handle == NULL) {
        ESP_LOGE(TAG, "NimBLE host static task create failed (internal stack)");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "NimBLE host task created on internal DRAM static stack");
    return ESP_OK;
}

bool ble_hid_is_connected(void)
{
    return s_ble_connected;
}

uint32_t ble_hid_get_disconnect_count(void)
{
    return ble_hid_disconnect_count_snapshot();
}

void ble_hid_register_usb_command_handler(ble_hid_usb_command_handler_t handler)
{
    s_usb_command_handler = handler;
}
