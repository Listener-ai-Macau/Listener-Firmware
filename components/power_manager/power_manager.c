#include "power_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "battery_monitor.h"
#include "board.h"
#include "board_pins.h"
#include "device_settings.h"
#include "diag_log.h"
#include "watchdog_platform.h"

extern esp_err_t audio_capture_set_idle_power_save(bool enabled) __attribute__((weak));
extern bool audio_capture_session_is_active(void) __attribute__((weak));
extern bool ble_hid_gap_is_connected(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_set_low_power_advertising(bool enabled) __attribute__((weak));
extern esp_err_t ble_hid_gap_stop_advertising_for_key_wake(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_low_power_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_reconnect(void) __attribute__((weak));
extern esp_err_t ble_hid_battery_force_refresh(const char *reason) __attribute__((weak));
extern void ble_hid_battery_task_wake(void) __attribute__((weak));
extern void system_health_set_low_power_mode(bool enabled) __attribute__((weak));
extern void status_led_set_low_power_disabled(bool disabled) __attribute__((weak));
extern void status_led_prepare_sleep(void) __attribute__((weak));
extern void status_led_notify_shutdown_confirm(bool final, const char *reason) __attribute__((weak));
extern void status_led_set_error(int domain, int severity, const char *reason) __attribute__((weak));

#define POWER_MANAGER_STATUS_LED_ERROR_DOMAIN_POWER 5
#define POWER_MANAGER_STATUS_LED_ERROR_RETRYABLE 0
#define POWER_MANAGER_STATUS_LED_ERROR_HARD 1

#ifndef CONFIG_POWER_MANAGER_ENABLE
#define CONFIG_POWER_MANAGER_ENABLE 1
#endif

#ifndef CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS
#define CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS 30000
#endif

#ifndef CONFIG_POWER_MANAGER_AUDIO_IDLE_MS
#define CONFIG_POWER_MANAGER_AUDIO_IDLE_MS 5000
#endif

#ifndef CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS
#define CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS 30000
#endif

#ifndef CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS
#define CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS 1800000
#endif

#ifndef CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS
#define CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS 2000
#endif

#define POWER_MANAGER_USB_PREFIX "POWER:"
#define POWER_MANAGER_BATTERY_WARN_PERCENT 10U
#define POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS 10000U
#define POWER_MANAGER_CHARGE_FULL_MIN_MV 4050U
#define POWER_MANAGER_CHARGE_FULL_MIN_PERCENT 88U
#define POWER_MANAGER_CHARGER_STATUS_EXTERNAL_HOLD_MS 8000U
#define POWER_MANAGER_IDLE_BATTERY_REFRESH_MS 600000U
#define POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS 60000U
#define POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV 2800U
#define POWER_MANAGER_LOW_BATTERY_CONFIRM_MS 5000U
#define POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS 15000U
#define POWER_MANAGER_SHUTDOWN_BATTERY_NOTIFY_WAIT_MS 100U
#define POWER_MANAGER_SHUTDOWN_LED_CONFIRM_MS 700U
#define POWER_MANAGER_POWER_REMOVAL_WAIT_MS 10000U
#define POWER_MANAGER_SHUTDOWN_FAILURE_RETRY_MS 900000U
#ifndef CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT
#define CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT 0
#endif
#define POWER_MANAGER_BATTERY_CRITICAL_PERCENT ((uint8_t)CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT)
#define POWER_MANAGER_TASK_STACK_BYTES (4 * 1024)
#define POWER_MANAGER_SHUTDOWN_USER_ACTION "short-press hardware power key for cold boot after PWR_HOLD/GPIO9 drive-high shutdown"
#define POWER_MANAGER_POWER_SOURCE_USB_PRESENT (1u << 0)
#define POWER_MANAGER_POWER_SOURCE_CHARGING (1u << 1)
#define POWER_MANAGER_POWER_SOURCE_CHARGE_FULL (1u << 2)
#define POWER_MANAGER_POWER_SOURCE_EXTERNAL_PRESENT (1u << 3)
#define POWER_MANAGER_POWER_SOURCE_AUTO_SHUTDOWN_BLOCKED (1u << 4)

#define POWER_MANAGER_POWER_HOLD_ACTION_SOURCE_SNAPSHOT 0u
#define POWER_MANAGER_POWER_HOLD_ACTION_RUNTIME_GUARD 1u
#define POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_ENTRY 2u
#define POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_DRIVE_HIGH 3u
#define POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_FAILED_RESTORE 4u
#define POWER_MANAGER_POWER_HOLD_ACTION_INIT 5u
#define POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_FAILURE_BACKOFF 6u

static const char *TAG = "power_manager";

typedef struct {
    int usb_det_level;
    bool usb_det_adc_valid;
    int usb_det_adc_mv;
    bool usb_det_mismatch;
    int bat_chg_level;
    int bat_std_level;
    int pwr_hold_level;
    bool usb_serial_jtag_sof_active;
    bool usb_power_present;
    bool charger_active;
    bool charge_power_present;
    bool external_power_present;
    bool charging;
    bool charge_full;
    bool charge_full_latched;
    uint32_t charge_full_candidate_ms;
    const char *usb_det_policy;
    const char *charger_polarity_policy;
    const char *pwr_hold_policy;
} power_manager_power_source_snapshot_t;

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task_handle;
static bool s_initialized;
static bool s_started;
static bool s_power_input_wake_configured;
static volatile bool s_power_input_irq_armed;
static bool s_ble_connected;
static bool s_battery_warning_logged;
static bool s_audio_idle_power_save_enabled;
static bool s_power_source_initialized;
static bool s_usb_power_present;
static bool s_external_power_present;
static bool s_charging;
static bool s_charge_full;
static bool s_charge_full_latched;
static bool s_auto_shutdown_block_logged;
static bool s_cached_battery_valid;
static int s_usb_det_level = -1;
static int s_bat_chg_level = -1;
static int s_bat_std_level = -1;
static int s_pwr_hold_level = -1;
static bool s_usb_serial_jtag_sof_active;
static uint64_t s_charge_full_candidate_since_ms;
static uint64_t s_charger_status_external_until_ms;
static uint64_t s_cached_battery_read_ms;
static uint32_t s_cached_battery_mv;
static uint8_t s_cached_battery_level_percent = 0xFF;
static uint64_t s_low_battery_critical_since_ms;
static uint32_t s_blockers;
static uint64_t s_last_user_activity_ms;
static uint64_t s_last_radio_activity_ms;
static power_manager_shutdown_reason_t s_last_shutdown_reason =
    POWER_MANAGER_SHUTDOWN_REASON_NONE;
static uint32_t s_last_shutdown_idle_ms;
static uint32_t s_last_shutdown_blockers;
static uint64_t s_shutdown_failure_retry_after_ms;
static esp_err_t s_last_shutdown_failure_ret = ESP_OK;
static power_manager_state_t s_state = POWER_MANAGER_STATE_ACTIVE;

static bool power_manager_gpio_is_valid(gpio_num_t gpio)
{
    return gpio != GPIO_NUM_NC && gpio >= 0 && gpio < GPIO_NUM_MAX;
}

static void power_manager_disable_power_input_interrupt_from_isr(gpio_num_t gpio)
{
    if (!power_manager_gpio_is_valid(gpio)) {
        return;
    }
    (void)gpio_intr_disable(gpio);
}

static void power_manager_power_input_wake_from_isr(void *arg)
{
    (void)arg;
    if (s_power_input_irq_armed) {
        s_power_input_irq_armed = false;
        power_manager_disable_power_input_interrupt_from_isr(BOARD_PINS_BAT_CHG_IO);
        power_manager_disable_power_input_interrupt_from_isr(BOARD_PINS_BAT_STD_IO);
    }

    TaskHandle_t task_handle = s_task_handle;
    if (task_handle == NULL) {
        return;
    }

    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task_handle, &higher_priority_woken);
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t power_manager_configure_power_input_wake_pin(gpio_num_t gpio, const char *name)
{
    if (!power_manager_gpio_is_valid(gpio)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input wake GPIO config failed: name=%s gpio=%d ret=%s",
                 name, (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(gpio, power_manager_power_input_wake_from_isr, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input ISR handler add failed: name=%s gpio=%d ret=%s",
                 name, (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_intr_disable(gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input ISR initial disarm failed: name=%s gpio=%d ret=%s",
                 name, (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input light-sleep wake enable failed: name=%s gpio=%d ret=%s",
                 name, (int)gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_intr_disable(gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input ISR disarm failed: name=%s gpio=%d ret=%s",
                 name, (int)gpio, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t power_manager_set_power_input_interrupt(gpio_num_t gpio, bool enabled)
{
    if (!power_manager_gpio_is_valid(gpio)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return enabled ? gpio_intr_enable(gpio) : gpio_intr_disable(gpio);
}

static esp_err_t power_manager_set_power_input_irq_armed(bool armed)
{
    if (!s_power_input_wake_configured) {
        return ESP_OK;
    }
    if (s_power_input_irq_armed == armed) {
        return ESP_OK;
    }

    esp_err_t chg_ret =
        power_manager_set_power_input_interrupt(BOARD_PINS_BAT_CHG_IO, armed);
    esp_err_t std_ret =
        power_manager_set_power_input_interrupt(BOARD_PINS_BAT_STD_IO, armed);
    if (chg_ret != ESP_OK && std_ret != ESP_OK) {
        return chg_ret != ESP_ERR_NOT_SUPPORTED ? chg_ret : std_ret;
    }

    s_power_input_irq_armed = armed;
    ESP_LOGI(TAG, "power input wake interrupt %s", armed ? "armed" : "disarmed");
    return ESP_OK;
}

static esp_err_t power_manager_configure_power_input_wake(void)
{
    if (s_power_input_wake_configured) {
        return ESP_OK;
    }

    board_v2_power_input_snapshot_t warmup_snapshot = {0};
    board_get_v2_power_input_snapshot(&warmup_snapshot);

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "power input GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_err_t chg_ret =
        power_manager_configure_power_input_wake_pin(BOARD_PINS_BAT_CHG_IO, "BAT_CHG");
    esp_err_t std_ret =
        power_manager_configure_power_input_wake_pin(BOARD_PINS_BAT_STD_IO, "BAT_STD");
    if (chg_ret != ESP_OK && std_ret != ESP_OK) {
        return chg_ret != ESP_ERR_NOT_SUPPORTED ? chg_ret : std_ret;
    }

    ret = esp_sleep_enable_gpio_wakeup();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power input GPIO light-sleep wake source enable failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    s_power_input_wake_configured = true;
    ESP_LOGI(
        TAG,
        "power input wake ready: BAT_CHG_GPIO=%d BAT_STD_GPIO=%d interrupt=one_shot_low_level light_sleep_wake=active_low",
        (int)BOARD_PINS_BAT_CHG_IO,
        (int)BOARD_PINS_BAT_STD_IO);
    return ESP_OK;
}

static void power_manager_update_power_input_irq_arm(
    power_manager_state_t state,
    const power_manager_power_source_snapshot_t *source)
{
    bool should_arm =
        source != NULL &&
        !source->external_power_present &&
        (state == POWER_MANAGER_STATE_CONNECTED_IDLE ||
         state == POWER_MANAGER_STATE_DISCONNECTED_IDLE);
    esp_err_t ret = power_manager_set_power_input_irq_armed(should_arm);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "power input wake interrupt %s failed: %s",
            should_arm ? "arm" : "disarm",
            esp_err_to_name(ret));
    }
}

static uint64_t power_manager_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static uint32_t power_manager_clamp_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static uint32_t power_manager_hardware_shutdown_ms(void)
{
    return device_settings_get_active_auto_shutdown_ms(s_external_power_present);
}

static bool power_manager_automatic_shutdown_enabled(void)
{
    return power_manager_hardware_shutdown_ms() > 0U;
}

static uint32_t power_manager_low_power_idle_ms(void)
{
    return device_settings_get_active_low_power_idle_ms(s_external_power_present);
}

static bool power_manager_plugged_low_power_enabled(void)
{
    return device_settings_get_plugged_low_power_enabled();
}

static bool power_manager_plugged_auto_shutdown_enabled(void)
{
    return device_settings_get_plugged_auto_shutdown_ms() > 0U;
}

const char *power_manager_state_name(power_manager_state_t state)
{
    switch (state) {
    case POWER_MANAGER_STATE_ACTIVE:
        return "ACTIVE";
    case POWER_MANAGER_STATE_CONNECTED_IDLE:
        return "CONNECTED_IDLE";
    case POWER_MANAGER_STATE_DISCONNECTED_IDLE:
        return "DISCONNECTED_IDLE";
    case POWER_MANAGER_STATE_HARDWARE_SHUTDOWN:
        return "HARDWARE_SHUTDOWN";
    default:
        return "UNKNOWN";
    }
}

const char *power_manager_shutdown_reason_name(power_manager_shutdown_reason_t reason)
{
    switch (reason) {
    case POWER_MANAGER_SHUTDOWN_REASON_NONE:
        return "none";
    case POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE:
        return "long_idle";
    case POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND:
        return "manual_command";
    case POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY:
        return "low_battery";
    default:
        return "unknown";
    }
}

static void power_manager_blocker_names(uint32_t blockers, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    struct {
        uint32_t mask;
        const char *name;
    } entries[] = {
        {POWER_MANAGER_BLOCKER_RECORDING, "recording"},
        {POWER_MANAGER_BLOCKER_BLE_AUDIO, "ble_audio"},
        {POWER_MANAGER_BLOCKER_DIAG_EXPORT, "diag_export"},
        {POWER_MANAGER_BLOCKER_PAIRING, "pairing"},
        {POWER_MANAGER_BLOCKER_RECONNECT, "reconnect"},
        {POWER_MANAGER_BLOCKER_FLASH_WRITE, "flash_write"},
        {POWER_MANAGER_BLOCKER_USB_COMMAND, "usb_command"},
        {POWER_MANAGER_BLOCKER_EXTERNAL_POWER, "external_power"},
    };

    bool first = true;
    for (size_t index = 0; index < sizeof(entries) / sizeof(entries[0]); ++index) {
        if ((blockers & entries[index].mask) == 0) {
            continue;
        }

        size_t used = strlen(buffer);
        if (used >= buffer_size) {
            break;
        }
        snprintf(
            buffer + used,
            buffer_size - used,
            "%s%s",
            first ? "" : "|",
            entries[index].name);
        first = false;
    }

    if (first) {
        snprintf(buffer, buffer_size, "none");
    }
}

static const char *power_manager_gpio_level_name(int level)
{
    if (level < 0) {
        return "nc";
    }
    return level != 0 ? "high" : "low";
}

static uint32_t power_manager_encode_gpio_level(int level)
{
    if (level < 0) {
        return 2u;
    }
    return level != 0 ? 1u : 0u;
}

static uint32_t power_manager_encode_power_source_levels(const power_manager_power_source_snapshot_t *source)
{
    if (source == NULL) {
        return 0x222u;
    }
    return power_manager_encode_gpio_level(source->usb_det_level) |
           (power_manager_encode_gpio_level(source->bat_chg_level) << 4) |
           (power_manager_encode_gpio_level(source->bat_std_level) << 8) |
           (power_manager_encode_gpio_level(source->pwr_hold_level) << 12);
}

static void power_manager_log_power_hold_diag(
    uint8_t severity,
    const board_v2_power_hold_snapshot_t *snapshot,
    uint32_t action)
{
    board_v2_power_hold_snapshot_t local_snapshot = {0};
    if (snapshot == NULL) {
        board_get_v2_power_hold_snapshot(&local_snapshot);
        snapshot = &local_snapshot;
    }
    diag_log(
        DIAG_SRC_POWER,
        DIAG_POWER_HOLD_STATE,
        severity,
        snapshot->configured ? 1u : 0u,
        snapshot->gpio >= 0 ? (uint32_t)snapshot->gpio : UINT32_MAX,
        power_manager_encode_gpio_level(snapshot->level),
        action);
}

static uint32_t power_manager_encode_power_source_flags(
    const power_manager_power_source_snapshot_t *source,
    bool automatic_shutdown_blocked)
{
    if (source == NULL) {
        return 0;
    }

    uint32_t flags = 0;
    if (source->usb_power_present) {
        flags |= POWER_MANAGER_POWER_SOURCE_USB_PRESENT;
    }
    if (source->charging) {
        flags |= POWER_MANAGER_POWER_SOURCE_CHARGING;
    }
    if (source->charge_full) {
        flags |= POWER_MANAGER_POWER_SOURCE_CHARGE_FULL;
    }
    if (source->external_power_present) {
        flags |= POWER_MANAGER_POWER_SOURCE_EXTERNAL_PRESENT;
    }
    if (automatic_shutdown_blocked) {
        flags |= POWER_MANAGER_POWER_SOURCE_AUTO_SHUTDOWN_BLOCKED;
    }
    return flags;
}

static bool power_manager_charge_full_battery_allowed(const power_manager_snapshot_t *battery_snapshot)
{
    return battery_snapshot == NULL ||
           !battery_snapshot->battery_valid ||
           battery_snapshot->battery_mv >= POWER_MANAGER_CHARGE_FULL_MIN_MV ||
           battery_snapshot->battery_level_percent >= POWER_MANAGER_CHARGE_FULL_MIN_PERCENT;
}

static uint32_t power_manager_charge_full_candidate_ms_locked(uint64_t now_ms)
{
    if (s_charge_full_candidate_since_ms == 0 || now_ms < s_charge_full_candidate_since_ms) {
        return 0;
    }
    return power_manager_clamp_u64_to_u32(now_ms - s_charge_full_candidate_since_ms);
}

static bool power_manager_charger_status_external_locked(
    bool usb_power_present,
    bool raw_charging,
    bool raw_full_external,
    uint64_t now_ms)
{
    if (usb_power_present || raw_charging || raw_full_external) {
        s_charger_status_external_until_ms =
            now_ms + POWER_MANAGER_CHARGER_STATUS_EXTERNAL_HOLD_MS;
        return true;
    }

    if (s_charger_status_external_until_ms != 0 &&
        now_ms < s_charger_status_external_until_ms) {
        return true;
    }

    s_charger_status_external_until_ms = 0;
    return false;
}

static void power_manager_apply_charge_state_filter_locked(
    power_manager_power_source_snapshot_t *source,
    uint64_t now_ms,
    const power_manager_snapshot_t *battery_snapshot)
{
    if (source == NULL) {
        return;
    }

    bool raw_charging = source->charger_active;
    bool raw_full_status = source->bat_std_level == 0;
    bool raw_full_external =
        raw_full_status &&
        power_manager_charge_full_battery_allowed(battery_snapshot);
    bool charger_status_external = power_manager_charger_status_external_locked(
        source->usb_power_present,
        raw_charging,
        raw_full_external,
        now_ms);
    source->charge_power_present = source->charge_power_present || charger_status_external;
    raw_charging = source->charge_power_present && raw_charging;
    bool raw_full = source->charge_power_present && raw_full_status;

    if (!source->charge_power_present) {
        s_charge_full_latched = false;
        s_charge_full_candidate_since_ms = 0;
    } else if (!s_charge_full_latched) {
        bool full_candidate =
            raw_full &&
            !raw_charging &&
            power_manager_charge_full_battery_allowed(battery_snapshot);
        if (full_candidate) {
            if (s_charge_full_candidate_since_ms == 0) {
                s_charge_full_candidate_since_ms = now_ms;
            } else if (
                now_ms - s_charge_full_candidate_since_ms >=
                POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS) {
                s_charge_full_latched = true;
            }
        } else {
            s_charge_full_candidate_since_ms = 0;
        }
    }

    source->charge_full = source->charge_power_present && s_charge_full_latched;
    source->charging = raw_charging && !source->charge_full;
    source->charge_power_present = source->charge_power_present || source->charge_full;
    source->external_power_present =
        source->usb_power_present || source->charging || source->charge_full || charger_status_external;
    source->charge_full_latched = s_charge_full_latched;
    source->charge_full_candidate_ms = power_manager_charge_full_candidate_ms_locked(now_ms);
}

static void power_manager_read_power_source(power_manager_power_source_snapshot_t *out_source)
{
    if (out_source == NULL) {
        return;
    }

    board_v2_power_input_snapshot_t board_snapshot = {0};
    board_get_v2_power_input_snapshot(&board_snapshot);

    bool usb_serial_jtag_sof_active = board_snapshot.usb_serial_jtag_sof_active;
    bool usb_power_present = board_snapshot.usb_power_present;
    bool charger_active = board_snapshot.bat_chg_level == 0;
    bool raw_full = board_snapshot.bat_std_level == 0;
    bool charge_power_present = usb_power_present || charger_active || raw_full;

    *out_source = (power_manager_power_source_snapshot_t){
        .usb_det_level = board_snapshot.usb_det_level,
        .usb_det_adc_valid = board_snapshot.usb_det_adc_valid,
        .usb_det_adc_mv = board_snapshot.usb_det_adc_mv,
        .usb_det_mismatch = board_snapshot.usb_det_mismatch,
        .bat_chg_level = board_snapshot.bat_chg_level,
        .bat_std_level = board_snapshot.bat_std_level,
        .pwr_hold_level = board_snapshot.pwr_hold_level,
        .usb_serial_jtag_sof_active = usb_serial_jtag_sof_active,
        .usb_power_present = usb_power_present,
        .charger_active = charger_active,
        .charge_power_present = charge_power_present,
        .external_power_present = usb_power_present || charger_active || raw_full,
        .charging = charger_active,
        .charge_full = charge_power_present && board_snapshot.bat_std_level == 0,
        .usb_det_policy = board_snapshot.usb_det_policy,
        .charger_polarity_policy = board_snapshot.charger_polarity_policy,
        .pwr_hold_policy = board_snapshot.pwr_hold_policy,
    };
    /*
     * GPIO7 USB_DET is disabled on this hardware revision because the pad is
     * unreliable at runtime. USB Serial/JTAG SOF identifies a computer USB host;
     * BAT_CHG is the active-charging signal, and BAT_STD can prove
     * charger-standby/full after the central debounce/near-full guard.
     */
}

static uint32_t power_manager_shutdown_blockers_for_source(
    uint32_t blockers,
    const power_manager_power_source_snapshot_t *source,
    power_manager_shutdown_reason_t reason)
{
    uint32_t shutdown_blockers = blockers;
    if (reason == POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE &&
        source != NULL &&
        source->external_power_present) {
        if (power_manager_plugged_auto_shutdown_enabled()) {
            shutdown_blockers &= ~(uint32_t)POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
        } else {
            shutdown_blockers |= POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
        }
    }
    if (reason == POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY &&
        source != NULL &&
        (source->usb_power_present ||
         source->external_power_present ||
         source->charging ||
         source->charge_full)) {
        shutdown_blockers |= POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
    }
    return shutdown_blockers;
}

static bool power_manager_low_battery_shutdown_allowed(
    const power_manager_power_source_snapshot_t *source)
{
    return source != NULL &&
           !source->usb_power_present &&
           !source->external_power_present &&
           !source->charging &&
           !source->charge_full;
}

static uint32_t power_manager_automatic_shutdown_blockers_for_source(
    uint32_t blockers,
    const power_manager_power_source_snapshot_t *source)
{
    return power_manager_shutdown_blockers_for_source(
        blockers,
        source,
        POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE);
}

static uint32_t power_manager_without_external_power_blocker(uint32_t blockers)
{
    return blockers & ~(uint32_t)POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
}

static uint32_t power_manager_awake_blockers(uint32_t blockers)
{
    return power_manager_without_external_power_blocker(blockers);
}

static uint32_t power_manager_audio_idle_blockers(uint32_t blockers)
{
    return power_manager_awake_blockers(blockers);
}

static power_manager_power_source_snapshot_t power_manager_cached_power_source_locked(void)
{
    return (power_manager_power_source_snapshot_t){
        .usb_det_level = s_usb_det_level,
        .bat_chg_level = s_bat_chg_level,
        .bat_std_level = s_bat_std_level,
        .pwr_hold_level = s_pwr_hold_level,
        .usb_serial_jtag_sof_active = s_usb_serial_jtag_sof_active,
        .usb_power_present = s_usb_power_present,
        .external_power_present = s_external_power_present,
        .charging = s_charging,
        .charge_full = s_charge_full,
        .charge_full_latched = s_charge_full_latched,
        .charge_full_candidate_ms = 0,
        .usb_det_policy = "",
        .charger_polarity_policy = "",
        .pwr_hold_policy = "",
    };
}

static void power_manager_log_power_source_diag(
    uint8_t severity,
    const power_manager_power_source_snapshot_t *source,
    uint32_t idle_ms,
    uint32_t shutdown_blockers,
    bool automatic_shutdown_blocked)
{
    diag_log(
        DIAG_SRC_POWER,
        DIAG_POWER_EXTERNAL_POWER,
        severity,
        power_manager_encode_power_source_flags(source, automatic_shutdown_blocked),
        power_manager_encode_power_source_levels(source),
        idle_ms,
        shutdown_blockers);
}

static void power_manager_log_power_transition_diag(
    const power_manager_power_source_snapshot_t *source,
    uint32_t idle_ms)
{
    if (source == NULL) {
        return;
    }

    uint32_t raw_levels = power_manager_encode_power_source_levels(source);
    diag_log(
        DIAG_SRC_POWER,
        DIAG_POWER_USB_DETECT,
        DIAG_SEV_INFO,
        power_manager_encode_gpio_level(source->usb_det_level),
        source->usb_power_present ? 1u : 0u,
        idle_ms,
        raw_levels);
    diag_log(
        DIAG_SRC_POWER,
        DIAG_POWER_CHARGE_STATE,
        DIAG_SEV_INFO,
        source->charging ? 1u : 0u,
        source->charge_full ? 1u : 0u,
        idle_ms,
        raw_levels);
    diag_log(
        DIAG_SRC_POWER,
        DIAG_POWER_HOLD_STATE,
        DIAG_SEV_INFO,
        1u,
        (uint32_t)BOARD_PINS_PWR_HOLD_IO,
        power_manager_encode_gpio_level(source->pwr_hold_level),
        POWER_MANAGER_POWER_HOLD_ACTION_SOURCE_SNAPSHOT);
}

static uint32_t power_manager_user_idle_ms_locked(uint64_t now_ms)
{
    if (now_ms < s_last_user_activity_ms) {
        return 0;
    }
    return power_manager_clamp_u64_to_u32(now_ms - s_last_user_activity_ms);
}

static uint32_t power_manager_shutdown_failure_retry_ms_left_locked(uint64_t now_ms)
{
    if (s_shutdown_failure_retry_after_ms == 0 || now_ms >= s_shutdown_failure_retry_after_ms) {
        return 0;
    }
    return power_manager_clamp_u64_to_u32(s_shutdown_failure_retry_after_ms - now_ms);
}

static bool power_manager_shutdown_failure_retry_active_locked(uint64_t now_ms)
{
    return power_manager_shutdown_failure_retry_ms_left_locked(now_ms) > 0;
}

static void power_manager_clear_shutdown_failure_retry_locked(void)
{
    s_shutdown_failure_retry_after_ms = 0;
    s_last_shutdown_failure_ret = ESP_OK;
}

static uint32_t power_manager_radio_idle_ms_locked(uint64_t now_ms)
{
    if (now_ms < s_last_radio_activity_ms) {
        return 0;
    }
    return power_manager_clamp_u64_to_u32(now_ms - s_last_radio_activity_ms);
}

static bool power_manager_sync_power_source_locked(
    const power_manager_power_source_snapshot_t *source,
    uint64_t now_ms)
{
    if (source == NULL) {
        return false;
    }

    bool state_changed = !s_power_source_initialized ||
                         s_usb_det_level != source->usb_det_level ||
                         s_pwr_hold_level != source->pwr_hold_level ||
                         s_usb_serial_jtag_sof_active != source->usb_serial_jtag_sof_active ||
                         s_usb_power_present != source->usb_power_present ||
                         s_external_power_present != source->external_power_present ||
                         s_charging != source->charging ||
                         s_charge_full != source->charge_full;
    bool raw_status_changed = !s_power_source_initialized ||
                              s_bat_chg_level != source->bat_chg_level ||
                              s_bat_std_level != source->bat_std_level;
    bool external_changed = !s_power_source_initialized ||
                            s_external_power_present != source->external_power_present;

    if (!state_changed && !raw_status_changed) {
        return false;
    }

    if (s_power_source_initialized && state_changed && external_changed) {
        s_last_user_activity_ms = now_ms;
        s_last_radio_activity_ms = now_ms;
        power_manager_clear_shutdown_failure_retry_locked();
    }

    s_power_source_initialized = true;
    s_usb_det_level = source->usb_det_level;
    s_bat_chg_level = source->bat_chg_level;
    s_bat_std_level = source->bat_std_level;
    s_pwr_hold_level = source->pwr_hold_level;
    s_usb_serial_jtag_sof_active = source->usb_serial_jtag_sof_active;
    s_usb_power_present = source->usb_power_present;
    s_external_power_present = source->external_power_present;
    s_charging = source->charging;
    s_charge_full = source->charge_full;
    if (state_changed) {
        s_auto_shutdown_block_logged = false;
    }

    /* External power blocks automatic hardware shutdown. Runtime low-power idle
       while plugged is opt-in through the persisted device setting. */
    if (source->external_power_present) {
        s_blockers |= POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
    } else {
        s_blockers &= ~(uint32_t)POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
    }
    return state_changed;
}

static power_manager_state_t power_manager_awake_idle_state_locked(uint32_t radio_idle_ms)
{
    if (s_external_power_present && !power_manager_plugged_low_power_enabled()) {
        return POWER_MANAGER_STATE_ACTIVE;
    }

    uint32_t low_power_idle_ms = power_manager_low_power_idle_ms();
    if (s_ble_connected) {
        return radio_idle_ms >= low_power_idle_ms
            ? POWER_MANAGER_STATE_CONNECTED_IDLE
            : POWER_MANAGER_STATE_ACTIVE;
    }

    return radio_idle_ms >= low_power_idle_ms
        ? POWER_MANAGER_STATE_DISCONNECTED_IDLE
        : POWER_MANAGER_STATE_ACTIVE;
}

static bool power_manager_automatic_shutdown_blocked_by_external_power_locked(uint64_t now_ms)
{
    uint32_t hardware_shutdown_ms = power_manager_hardware_shutdown_ms();
    return CONFIG_POWER_MANAGER_ENABLE &&
           hardware_shutdown_ms > 0U &&
           power_manager_without_external_power_blocker(s_blockers) == 0 &&
           s_external_power_present &&
           !power_manager_plugged_auto_shutdown_enabled() &&
           power_manager_user_idle_ms_locked(now_ms) >=
               hardware_shutdown_ms;
}

static power_manager_state_t power_manager_target_state_locked(uint64_t now_ms)
{
    uint32_t user_idle_ms = power_manager_user_idle_ms_locked(now_ms);
    uint32_t radio_idle_ms = power_manager_radio_idle_ms_locked(now_ms);
    uint32_t hardware_shutdown_ms = power_manager_hardware_shutdown_ms();
    if (power_manager_awake_blockers(s_blockers) != 0) {
        return POWER_MANAGER_STATE_ACTIVE;
    }

    if (CONFIG_POWER_MANAGER_ENABLE &&
        hardware_shutdown_ms > 0U &&
        user_idle_ms >= hardware_shutdown_ms) {
        power_manager_power_source_snapshot_t source = power_manager_cached_power_source_locked();
        uint32_t shutdown_blockers =
            power_manager_automatic_shutdown_blockers_for_source(s_blockers, &source);
        if (shutdown_blockers != 0 ||
            power_manager_shutdown_failure_retry_active_locked(now_ms)) {
            return power_manager_awake_idle_state_locked(radio_idle_ms);
        }
        return POWER_MANAGER_STATE_HARDWARE_SHUTDOWN;
    }

    return power_manager_awake_idle_state_locked(radio_idle_ms);
}

static bool power_manager_should_preserve_idle_for_ble_change_locked(uint64_t now_ms)
{
    return power_manager_awake_blockers(s_blockers) == 0 &&
           power_manager_user_idle_ms_locked(now_ms) >=
               (uint32_t)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
}

static void power_manager_apply_ble_connection_change_locked(
    bool connected,
    uint64_t now_ms)
{
    s_ble_connected = connected;
    if (s_state == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN) {
        return;
    }

    if (power_manager_should_preserve_idle_for_ble_change_locked(now_ms)) {
        s_state = power_manager_target_state_locked(now_ms);
        return;
    }

    s_last_radio_activity_ms = now_ms;
    s_auto_shutdown_block_logged = false;
    s_state = POWER_MANAGER_STATE_ACTIVE;
}

static bool power_manager_refresh_ble_connection_locked(uint64_t now_ms)
{
    bool connected = ble_hid_gap_is_connected != NULL && ble_hid_gap_is_connected();
    if (s_ble_connected == connected) {
        return false;
    }

    power_manager_apply_ble_connection_change_locked(connected, now_ms);
    return true;
}

static bool power_manager_low_battery_shutdown_confirmed_locked(
    const power_manager_snapshot_t *battery_snapshot,
    const power_manager_power_source_snapshot_t *source,
    uint64_t now_ms)
{
    if (battery_snapshot == NULL || source == NULL) {
        return false;
    }
    if (!battery_snapshot->battery_valid ||
        battery_snapshot->battery_mv == 0 ||
        battery_snapshot->battery_mv > POWER_MANAGER_LOW_BATTERY_SHUTDOWN_MAX_MV ||
        battery_snapshot->battery_level_percent > POWER_MANAGER_BATTERY_CRITICAL_PERCENT) {
        s_low_battery_critical_since_ms = 0;
        return false;
    }
    if (!power_manager_low_battery_shutdown_allowed(source)) {
        s_low_battery_critical_since_ms = 0;
        return false;
    }
    if (s_low_battery_critical_since_ms == 0 || now_ms < s_low_battery_critical_since_ms) {
        s_low_battery_critical_since_ms = now_ms;
        return false;
    }
    if (now_ms - s_low_battery_critical_since_ms < POWER_MANAGER_LOW_BATTERY_CONFIRM_MS) {
        return false;
    }
    return power_manager_user_idle_ms_locked(now_ms) >= POWER_MANAGER_LOW_BATTERY_BOOT_GRACE_MS;
}

static void power_manager_log_transition(
    power_manager_state_t previous,
    power_manager_state_t next,
    uint32_t idle_ms,
    uint32_t blockers)
{
    if (previous == next) {
        return;
    }

    char blocker_text[96];
    power_manager_blocker_names(blockers, blocker_text, sizeof(blocker_text));
    ESP_LOGI(
        TAG,
        "state %s -> %s idle_ms=%" PRIu32 " blockers=0x%08" PRIx32 " (%s) ble_connected=%u",
        power_manager_state_name(previous),
        power_manager_state_name(next),
        idle_ms,
        blockers,
        blocker_text,
        s_ble_connected ? 1u : 0u);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_STATE, DIAG_SEV_INFO,
             (uint32_t)previous, (uint32_t)next, idle_ms, blockers);
}

static void power_manager_set_audio_idle_power_save(bool enabled)
{
    if (s_audio_idle_power_save_enabled == enabled || audio_capture_set_idle_power_save == NULL) {
        return;
    }

    esp_err_t ret = audio_capture_set_idle_power_save(enabled);
    if (ret == ESP_OK) {
        s_audio_idle_power_save_enabled = enabled;
    } else if (ret != ESP_ERR_INVALID_STATE || enabled) {
        ESP_LOGW(TAG, "audio idle power save request failed enabled=%u ret=%s",
                 enabled ? 1u : 0u, esp_err_to_name(ret));
    }
}

static void power_manager_apply_state(power_manager_state_t previous, power_manager_state_t next)
{
    if (previous == next) {
        return;
    }

    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }

    switch (next) {
    case POWER_MANAGER_STATE_ACTIVE:
        if (status_led_set_low_power_disabled != NULL) {
            status_led_set_low_power_disabled(false);
        }
        power_manager_set_audio_idle_power_save(false);
        if (system_health_set_low_power_mode != NULL) {
            system_health_set_low_power_mode(false);
        }
        if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(false);
        }
        if (s_ble_connected && ble_hid_gap_request_active_connection != NULL) {
            (void)ble_hid_gap_request_active_connection();
        }
        break;
    case POWER_MANAGER_STATE_CONNECTED_IDLE:
        if (status_led_set_low_power_disabled != NULL) {
            status_led_set_low_power_disabled(true);
        }
        power_manager_set_audio_idle_power_save(true);
        if (system_health_set_low_power_mode != NULL) {
            system_health_set_low_power_mode(true);
        }
        if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(false);
        }
        if (ble_hid_gap_request_low_power_connection != NULL) {
            (void)ble_hid_gap_request_low_power_connection();
        }
        break;
    case POWER_MANAGER_STATE_DISCONNECTED_IDLE:
    case POWER_MANAGER_STATE_HARDWARE_SHUTDOWN:
        if (status_led_set_low_power_disabled != NULL) {
            status_led_set_low_power_disabled(true);
        }
        power_manager_set_audio_idle_power_save(true);
        if (system_health_set_low_power_mode != NULL) {
            system_health_set_low_power_mode(true);
        }
        if (next == POWER_MANAGER_STATE_DISCONNECTED_IDLE &&
            s_external_power_present &&
            ble_hid_gap_set_low_power_advertising != NULL) {
            /*
             * USB/external power can still use idle audio/LED savings, but
             * keeping connectable advertising avoids Windows getting stuck
             * with only cached GATT services after a host Bluetooth restart.
             */
            (void)ble_hid_gap_set_low_power_advertising(false);
        } else if (next == POWER_MANAGER_STATE_DISCONNECTED_IDLE &&
            ble_hid_gap_stop_advertising_for_key_wake != NULL) {
            (void)ble_hid_gap_stop_advertising_for_key_wake();
        } else if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(true);
        }
        break;
    default:
        break;
    }

    if (ble_hid_battery_task_wake != NULL) {
        ble_hid_battery_task_wake();
    }
}

static void power_manager_apply_fast_idle_actions(power_manager_state_t state, uint32_t user_idle_ms, uint32_t blockers)
{
    if (state != POWER_MANAGER_STATE_ACTIVE) {
        return;
    }

    bool audio_idle = power_manager_audio_idle_blockers(blockers) == 0 &&
        user_idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
    power_manager_set_audio_idle_power_save(audio_idle);
}

static void power_manager_guard_runtime_power_hold_low(power_manager_state_t state)
{
    if (state == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN) {
        return;
    }

    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    if (power_hold.configured && power_hold.level == 0) {
        return;
    }

    ESP_LOGW(
        TAG,
        "PWR_HOLD/GPIO9 runtime guard reasserting low: state=%s level=%s configured=%u policy=%s",
        power_manager_state_name(state),
        power_manager_gpio_level_name(power_hold.level),
        power_hold.configured ? 1u : 0u,
        power_hold.policy != NULL ? power_hold.policy : "unknown");
    power_manager_log_power_hold_diag(
        DIAG_SEV_WARN,
        &power_hold,
        POWER_MANAGER_POWER_HOLD_ACTION_RUNTIME_GUARD);
    esp_err_t ret = board_set_power_hold_enabled(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWR_HOLD/GPIO9 runtime-low guard failed: %s", esp_err_to_name(ret));
    }
}

static void power_manager_update_battery_snapshot(power_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    battery_monitor_status_t battery = {0};
    esp_err_t ret = battery_monitor_read(&battery);
    snapshot->battery_valid = ret == ESP_OK && battery.valid;
    snapshot->battery_mv = snapshot->battery_valid ? battery.voltage_mv : 0;
    snapshot->battery_level_percent = snapshot->battery_valid ? battery.level_percent : 0xFF;

    if (snapshot->battery_valid &&
        snapshot->battery_level_percent < POWER_MANAGER_BATTERY_WARN_PERCENT &&
        !s_battery_warning_logged) {
        s_battery_warning_logged = true;
        diag_log(DIAG_SRC_POWER, DIAG_POWER_BATTERY_WARN, DIAG_SEV_WARN,
                 snapshot->battery_level_percent, snapshot->battery_mv, 0, 0);
    }
}

static void power_manager_store_battery_snapshot_locked(
    const power_manager_snapshot_t *snapshot,
    uint64_t now_ms)
{
    if (snapshot == NULL) {
        return;
    }

    s_cached_battery_valid = snapshot->battery_valid;
    s_cached_battery_mv = snapshot->battery_mv;
    s_cached_battery_level_percent = snapshot->battery_level_percent;
    s_cached_battery_read_ms = now_ms;
}

static bool power_manager_copy_cached_battery_snapshot_locked(power_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_cached_battery_read_ms == 0) {
        return false;
    }

    snapshot->battery_valid = s_cached_battery_valid;
    snapshot->battery_mv = s_cached_battery_mv;
    snapshot->battery_level_percent = s_cached_battery_level_percent;
    return true;
}

static bool power_manager_should_refresh_battery_for_evaluate_locked(
    uint64_t now_ms,
    const power_manager_power_source_snapshot_t *source)
{
    if (s_cached_battery_read_ms == 0) {
        return true;
    }
    if (s_state != POWER_MANAGER_STATE_DISCONNECTED_IDLE || s_ble_connected) {
        return true;
    }
    if (source != NULL &&
        (source->usb_power_present != s_usb_power_present ||
         source->external_power_present != s_external_power_present)) {
        return true;
    }
    if (now_ms < s_cached_battery_read_ms) {
        return true;
    }

    return now_ms - s_cached_battery_read_ms >= POWER_MANAGER_IDLE_BATTERY_REFRESH_MS;
}

static bool power_manager_should_refresh_battery_for_snapshot_locked(
    const power_manager_power_source_snapshot_t *source)
{
    if (s_cached_battery_read_ms == 0) {
        return true;
    }
    if (s_state != POWER_MANAGER_STATE_DISCONNECTED_IDLE || s_ble_connected) {
        return true;
    }
    if (source != NULL &&
        (source->usb_power_present != s_usb_power_present ||
         source->external_power_present != s_external_power_present)) {
        return true;
    }

    return false;
}

power_manager_state_t power_manager_get_state(void)
{
    power_manager_state_t state = s_state;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        state = s_state;
        xSemaphoreGive(s_mutex);
    }
    return state;
}

void power_manager_get_snapshot(power_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    uint64_t now_ms = power_manager_now_ms();
    power_manager_power_source_snapshot_t power_source = {0};
    power_manager_snapshot_t battery_snapshot = {0};
    bool refresh_battery = true;
    bool battery_refreshed = false;
    power_manager_read_power_source(&power_source);

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        refresh_battery =
            power_manager_should_refresh_battery_for_snapshot_locked(&power_source);
        if (!refresh_battery &&
            !power_manager_copy_cached_battery_snapshot_locked(&battery_snapshot)) {
            refresh_battery = true;
        }
        xSemaphoreGive(s_mutex);
    }

    if (refresh_battery) {
        power_manager_update_battery_snapshot(&battery_snapshot);
        battery_refreshed = true;
    }

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (battery_refreshed) {
            power_manager_store_battery_snapshot_locked(&battery_snapshot, now_ms);
        }
        power_manager_apply_charge_state_filter_locked(&power_source, now_ms, &battery_snapshot);
        snapshot->usb_det_level = power_source.usb_det_level;
        snapshot->usb_det_adc_valid = power_source.usb_det_adc_valid;
        snapshot->usb_det_adc_mv = power_source.usb_det_adc_mv;
        snapshot->usb_det_mismatch = power_source.usb_det_mismatch;
        snapshot->bat_chg_level = power_source.bat_chg_level;
        snapshot->bat_std_level = power_source.bat_std_level;
        snapshot->pwr_hold_level = power_source.pwr_hold_level;
        snapshot->usb_serial_jtag_sof_active = power_source.usb_serial_jtag_sof_active;
        snapshot->usb_power_present = power_source.usb_power_present;
        snapshot->charger_active = power_source.charger_active;
        snapshot->charge_power_present = power_source.charge_power_present;
        snapshot->external_power_present = power_source.external_power_present;
        snapshot->charging = power_source.charging;
        snapshot->charge_full = power_source.charge_full;
        snapshot->charge_full_latched = power_source.charge_full_latched;
        snapshot->charge_full_candidate_ms = power_source.charge_full_candidate_ms;
        snapshot->usb_det_policy = power_source.usb_det_policy;
        snapshot->charger_polarity_policy = power_source.charger_polarity_policy;
        snapshot->pwr_hold_policy = power_source.pwr_hold_policy;
        snapshot->state = s_state;
        snapshot->blockers = s_blockers;
        snapshot->audio_idle_blockers = power_manager_audio_idle_blockers(s_blockers);
        snapshot->shutdown_blockers =
            power_manager_automatic_shutdown_blockers_for_source(s_blockers, &power_source);
        snapshot->user_idle_ms = power_manager_user_idle_ms_locked(now_ms);
        snapshot->radio_idle_ms = power_manager_radio_idle_ms_locked(now_ms);
        snapshot->idle_ms = snapshot->user_idle_ms;
        snapshot->ble_connected = s_ble_connected;
        snapshot->audio_idle_power_save_enabled = s_audio_idle_power_save_enabled;
        uint32_t hardware_shutdown_ms = power_manager_hardware_shutdown_ms();
        snapshot->automatic_shutdown_blocked_by_external_power =
            CONFIG_POWER_MANAGER_ENABLE &&
            hardware_shutdown_ms > 0U &&
            power_manager_without_external_power_blocker(s_blockers) == 0 &&
            power_source.external_power_present &&
            !power_manager_plugged_auto_shutdown_enabled() &&
            snapshot->user_idle_ms >= hardware_shutdown_ms;
        snapshot->last_shutdown_reason = s_last_shutdown_reason;
        snapshot->last_shutdown_idle_ms = s_last_shutdown_idle_ms;
        snapshot->last_shutdown_blockers = s_last_shutdown_blockers;
        snapshot->last_shutdown_failure_ret = s_last_shutdown_failure_ret;
        snapshot->shutdown_failure_retry_ms_left =
            power_manager_shutdown_failure_retry_ms_left_locked(now_ms);
        xSemaphoreGive(s_mutex);
    } else {
        snapshot->usb_det_level = power_source.usb_det_level;
        snapshot->usb_det_adc_valid = power_source.usb_det_adc_valid;
        snapshot->usb_det_adc_mv = power_source.usb_det_adc_mv;
        snapshot->usb_det_mismatch = power_source.usb_det_mismatch;
        snapshot->bat_chg_level = power_source.bat_chg_level;
        snapshot->bat_std_level = power_source.bat_std_level;
        snapshot->pwr_hold_level = power_source.pwr_hold_level;
        snapshot->usb_serial_jtag_sof_active = power_source.usb_serial_jtag_sof_active;
        snapshot->usb_power_present = power_source.usb_power_present;
        snapshot->charger_active = power_source.charger_active;
        snapshot->charge_power_present = power_source.charge_power_present;
        snapshot->external_power_present = power_source.external_power_present;
        snapshot->charging = power_source.charging;
        snapshot->charge_full = power_source.charge_full;
        snapshot->usb_det_policy = power_source.usb_det_policy;
        snapshot->charger_polarity_policy = power_source.charger_polarity_policy;
        snapshot->pwr_hold_policy = power_source.pwr_hold_policy;
    }

    snapshot->battery_valid = battery_snapshot.battery_valid;
    snapshot->battery_mv = battery_snapshot.battery_mv;
    snapshot->battery_level_percent = battery_snapshot.battery_level_percent;
    snapshot->charge_full_debounce_ms = POWER_MANAGER_CHARGE_FULL_DEBOUNCE_MS;
    snapshot->charge_full_min_mv = POWER_MANAGER_CHARGE_FULL_MIN_MV;
    snapshot->charge_full_min_percent = POWER_MANAGER_CHARGE_FULL_MIN_PERCENT;
    snapshot->audio_idle_threshold_ms = CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
    snapshot->low_power_idle_threshold_ms = power_manager_low_power_idle_ms();
    snapshot->plugged_low_power_idle_threshold_ms = device_settings_get_plugged_low_power_idle_ms();
    snapshot->battery_low_power_idle_threshold_ms = device_settings_get_battery_low_power_idle_ms();
    snapshot->connected_idle_threshold_ms = snapshot->low_power_idle_threshold_ms;
    snapshot->disconnected_idle_threshold_ms = snapshot->low_power_idle_threshold_ms;
        snapshot->plugged_low_power_enabled = power_manager_plugged_low_power_enabled();
        snapshot->low_power_idle_allowed =
            !snapshot->external_power_present || snapshot->plugged_low_power_enabled;
        snapshot->power_input_wake_configured = s_power_input_wake_configured;
        snapshot->power_input_irq_armed = s_power_input_irq_armed;
        snapshot->hardware_shutdown_threshold_ms = power_manager_hardware_shutdown_ms();
    snapshot->plugged_auto_shutdown_threshold_ms = device_settings_get_plugged_auto_shutdown_ms();
    snapshot->battery_auto_shutdown_threshold_ms = device_settings_get_battery_auto_shutdown_ms();
    snapshot->shutdown_failure_retry_ms = POWER_MANAGER_SHUTDOWN_FAILURE_RETRY_MS;
    snapshot->hardware_shutdown_guard_enabled = CONFIG_POWER_MANAGER_ENABLE != 0;
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    snapshot->pwr_hold_gpio = power_hold.gpio;
    snapshot->pwr_hold_level = power_hold.level;
    snapshot->pwr_hold_configured = power_hold.configured;
    snapshot->pwr_hold_policy = power_hold.policy;
    snapshot->voice_key_gpio = (uint32_t)BOARD_PINS_EC11_KEY_IO;
    snapshot->hardware_shutdown_user_action = POWER_MANAGER_SHUTDOWN_USER_ACTION;
}

static void power_manager_reset_idle_after_shutdown_failure(void)
{
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        uint64_t reset_ms = power_manager_now_ms();
        s_last_user_activity_ms = reset_ms;
        s_last_radio_activity_ms = reset_ms;
        s_auto_shutdown_block_logged = false;
        power_manager_clear_shutdown_failure_retry_locked();
        s_state = s_ble_connected
            ? POWER_MANAGER_STATE_CONNECTED_IDLE
            : POWER_MANAGER_STATE_DISCONNECTED_IDLE;
        xSemaphoreGive(s_mutex);
    }
}

static void power_manager_notify_power_led_error(bool hard, const char *reason)
{
    if (status_led_set_error != NULL) {
        status_led_set_error(
            POWER_MANAGER_STATUS_LED_ERROR_DOMAIN_POWER,
            hard ? POWER_MANAGER_STATUS_LED_ERROR_HARD : POWER_MANAGER_STATUS_LED_ERROR_RETRYABLE,
            reason);
    }
}

static void power_manager_schedule_shutdown_failure_retry(
    power_manager_shutdown_reason_t reason,
    uint32_t final_idle_ms,
    esp_err_t failure_ret)
{
    uint32_t retry_ms_left = 0;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        uint64_t now_ms = power_manager_now_ms();
        s_shutdown_failure_retry_after_ms = now_ms + POWER_MANAGER_SHUTDOWN_FAILURE_RETRY_MS;
        s_last_shutdown_failure_ret = failure_ret;
        if (s_state == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN) {
            s_state = s_ble_connected
                ? POWER_MANAGER_STATE_CONNECTED_IDLE
                : POWER_MANAGER_STATE_DISCONNECTED_IDLE;
        }
        retry_ms_left = power_manager_shutdown_failure_retry_ms_left_locked(now_ms);
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGW(
        TAG,
        "hardware shutdown failed; staying in low-power idle until retry window expires"
        " reason=%s idle_ms=%" PRIu32 " ret=%s retry_ms=%" PRIu32,
        power_manager_shutdown_reason_name(reason),
        final_idle_ms,
        esp_err_to_name(failure_ret),
        retry_ms_left);
    power_manager_log_power_hold_diag(
        DIAG_SEV_WARN,
        NULL,
        POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_FAILURE_BACKOFF);
}

static void power_manager_restore_after_shutdown_failure(
    power_manager_shutdown_reason_t reason,
    uint32_t final_idle_ms,
    esp_err_t failure_ret)
{
    esp_err_t restore_ret = board_set_power_hold_enabled(true);
    if (restore_ret != ESP_OK) {
        ESP_LOGE(TAG, "PWR_HOLD/GPIO9 runtime-low restore failed after shutdown failure: %s",
                 esp_err_to_name(restore_ret));
    }
    power_manager_log_power_hold_diag(
        restore_ret == ESP_OK ? DIAG_SEV_WARN : DIAG_SEV_ERROR,
        NULL,
        POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_FAILED_RESTORE);
    if (reason == POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND &&
        ble_hid_gap_request_reconnect != NULL) {
        (void)ble_hid_gap_request_reconnect();
    }
    diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_ERROR,
             0, final_idle_ms, (uint32_t)reason, (uint32_t)failure_ret);
    power_manager_notify_power_led_error(true, "hardware_shutdown_failed");
    if (reason == POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND) {
        power_manager_reset_idle_after_shutdown_failure();
    } else {
        power_manager_schedule_shutdown_failure_retry(reason, final_idle_ms, failure_ret);
    }
}

static esp_err_t power_manager_enter_hardware_shutdown(power_manager_shutdown_reason_t reason)
{
    if (!CONFIG_POWER_MANAGER_ENABLE) {
        ESP_LOGW(TAG, "hardware shutdown rejected: power manager disabled");
        return ESP_ERR_INVALID_STATE;
    }

    power_manager_snapshot_t snapshot;
    power_manager_get_snapshot(&snapshot);

    if (reason == POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE &&
        snapshot.external_power_present) {
        power_manager_power_source_snapshot_t power_source = {
            .usb_det_level = snapshot.usb_det_level,
            .usb_det_adc_valid = snapshot.usb_det_adc_valid,
            .usb_det_adc_mv = snapshot.usb_det_adc_mv,
            .usb_det_mismatch = snapshot.usb_det_mismatch,
            .bat_chg_level = snapshot.bat_chg_level,
            .bat_std_level = snapshot.bat_std_level,
            .usb_power_present = snapshot.usb_power_present,
            .charger_active = snapshot.charger_active,
            .charge_power_present = snapshot.charge_power_present,
            .external_power_present = snapshot.external_power_present,
            .charging = snapshot.charging,
            .charge_full = snapshot.charge_full,
            .usb_det_policy = snapshot.usb_det_policy,
            .charger_polarity_policy = snapshot.charger_polarity_policy,
        };
        uint32_t shutdown_blockers = power_manager_automatic_shutdown_blockers_for_source(
            snapshot.blockers,
            &power_source);
        char blocker_text[96];
        power_manager_blocker_names(shutdown_blockers, blocker_text, sizeof(blocker_text));
        ESP_LOGW(
            TAG,
            "hardware shutdown rejected: automatic hardware shutdown blocked by external power"
            " shutdown_blockers=0x%08" PRIx32 " (%s) idle_ms=%" PRIu32
            " usb_det_level=%s charging=%u charge_full=%u external_power_present=%u"
            " charger_policy=%s",
            shutdown_blockers,
            blocker_text,
            snapshot.idle_ms,
            power_manager_gpio_level_name(snapshot.usb_det_level),
            snapshot.charging ? 1u : 0u,
            snapshot.charge_full ? 1u : 0u,
            snapshot.external_power_present ? 1u : 0u,
            snapshot.charger_polarity_policy != NULL ? snapshot.charger_polarity_policy : "unknown");
        diag_log(
            DIAG_SRC_POWER,
            DIAG_POWER_SLEEP_BLOCKED,
            DIAG_SEV_WARN,
            shutdown_blockers,
            snapshot.idle_ms,
            (uint32_t)reason,
            power_manager_encode_power_source_flags(&power_source, true));
        power_manager_log_power_source_diag(
            DIAG_SEV_WARN,
            &power_source,
            snapshot.idle_ms,
            shutdown_blockers,
            true);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t entry_blockers = reason == POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE
        ? snapshot.blockers
        : power_manager_without_external_power_blocker(snapshot.blockers);
    if (entry_blockers != 0) {
        char blocker_text[96];
        power_manager_blocker_names(entry_blockers, blocker_text, sizeof(blocker_text));
        ESP_LOGW(
            TAG,
            "hardware shutdown rejected: blockers=0x%08" PRIx32 " (%s) idle_ms=%" PRIu32,
            entry_blockers,
            blocker_text,
            snapshot.idle_ms);
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 entry_blockers, snapshot.idle_ms, (uint32_t)reason, 0);
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_capture_session_is_active != NULL && audio_capture_session_is_active()) {
        ESP_LOGW(TAG, "hardware shutdown rejected: audio capture session active");
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 POWER_MANAGER_BLOCKER_RECORDING, snapshot.idle_ms, (uint32_t)reason, 0);
        return ESP_ERR_INVALID_STATE;
    }

    power_manager_power_source_snapshot_t final_power_source = {0};
    power_manager_snapshot_t final_battery_snapshot = {0};
    power_manager_read_power_source(&final_power_source);
    power_manager_update_battery_snapshot(&final_battery_snapshot);

    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "hardware shutdown rejected: failed to acquire final shutdown gate");
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 0, snapshot.idle_ms, (uint32_t)reason, (uint32_t)ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    power_manager_apply_charge_state_filter_locked(
        &final_power_source,
        power_manager_now_ms(),
        &final_battery_snapshot);
    uint32_t final_blockers = reason == POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE
        ? s_blockers
        : power_manager_without_external_power_blocker(s_blockers);
    uint32_t final_shutdown_blockers = power_manager_shutdown_blockers_for_source(
        final_blockers,
        &final_power_source,
        reason);
    uint32_t final_idle_ms = power_manager_user_idle_ms_locked(power_manager_now_ms());
    if (final_shutdown_blockers != 0) {
        xSemaphoreGive(s_mutex);
        char blocker_text[96];
        power_manager_blocker_names(final_shutdown_blockers, blocker_text, sizeof(blocker_text));
        ESP_LOGW(
            TAG,
            "hardware shutdown rejected: final shutdown_blockers=0x%08" PRIx32
            " (%s) idle_ms=%" PRIu32
            " external_power_present=%u charging=%u charge_full=%u usb_det_level=%s",
            final_shutdown_blockers,
            blocker_text,
            final_idle_ms,
            final_power_source.external_power_present ? 1u : 0u,
            final_power_source.charging ? 1u : 0u,
            final_power_source.charge_full ? 1u : 0u,
            power_manager_gpio_level_name(final_power_source.usb_det_level));
        diag_log(
            DIAG_SRC_POWER,
            DIAG_POWER_SLEEP_BLOCKED,
            DIAG_SEV_WARN,
            final_shutdown_blockers,
            final_idle_ms,
            (uint32_t)reason,
            power_manager_encode_power_source_flags(
                &final_power_source,
                (final_shutdown_blockers & POWER_MANAGER_BLOCKER_EXTERNAL_POWER) != 0));
        if ((final_shutdown_blockers & POWER_MANAGER_BLOCKER_EXTERNAL_POWER) != 0) {
            power_manager_log_power_source_diag(
                DIAG_SEV_WARN,
                &final_power_source,
                final_idle_ms,
                final_shutdown_blockers,
                true);
        }
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_capture_session_is_active != NULL && audio_capture_session_is_active()) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "hardware shutdown rejected: final audio capture session active");
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 POWER_MANAGER_BLOCKER_RECORDING, final_idle_ms, (uint32_t)reason, 0);
        return ESP_ERR_INVALID_STATE;
    }

    s_last_shutdown_reason = reason;
    s_last_shutdown_idle_ms = final_idle_ms;
    s_last_shutdown_blockers = final_blockers;
    s_state = POWER_MANAGER_STATE_HARDWARE_SHUTDOWN;
    xSemaphoreGive(s_mutex);

    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    power_manager_log_power_hold_diag(
        DIAG_SEV_WARN,
        &power_hold,
        POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_ENTRY);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_ENTRY, DIAG_SEV_INFO,
             final_idle_ms,
             snapshot.battery_valid ? snapshot.battery_mv : 0,
             snapshot.battery_valid ? snapshot.battery_level_percent : 0xFF,
             (uint32_t)reason);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_STATUS, DIAG_SEV_INFO,
             (uint32_t)POWER_MANAGER_STATE_HARDWARE_SHUTDOWN,
             final_blockers,
             final_idle_ms,
             power_hold.gpio >= 0 ? (uint32_t)power_hold.gpio : UINT32_MAX);
    ESP_LOGW(
        TAG,
        "entering hardware shutdown reason=%s idle_ms=%" PRIu32 " battery_mv=%" PRIu32
        " level=%u pwr_hold_gpio=%d pwr_hold_level=%s pwr_hold_configured=%u"
        " pwr_hold_policy=%s user_action=\"%s\"",
        power_manager_shutdown_reason_name(reason),
        final_idle_ms,
        snapshot.battery_mv,
        snapshot.battery_level_percent,
        power_hold.gpio,
        power_manager_gpio_level_name(power_hold.level),
        power_hold.configured ? 1u : 0u,
        power_hold.policy != NULL ? power_hold.policy : "unknown",
        POWER_MANAGER_SHUTDOWN_USER_ACTION);

    if (status_led_notify_shutdown_confirm != NULL) {
        status_led_notify_shutdown_confirm(true, "hardware_shutdown_confirmed");
        vTaskDelay(pdMS_TO_TICKS(POWER_MANAGER_SHUTDOWN_LED_CONFIRM_MS));
    }

    if (ble_hid_battery_force_refresh != NULL) {
        esp_err_t battery_notify_ret = ble_hid_battery_force_refresh("pre_shutdown");
        if (battery_notify_ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(POWER_MANAGER_SHUTDOWN_BATTERY_NOTIFY_WAIT_MS));
        } else if (battery_notify_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(
                TAG,
                "shutdown pre-disconnect battery refresh failed: %s",
                esp_err_to_name(battery_notify_ret));
        }
    }

    if (status_led_prepare_sleep != NULL) {
        status_led_prepare_sleep();
    }
    power_manager_set_audio_idle_power_save(true);
    if (system_health_set_low_power_mode != NULL) {
        system_health_set_low_power_mode(true);
    }
    if (ble_hid_gap_prepare_shutdown_disconnect != NULL) {
        esp_err_t ble_ret = ble_hid_gap_prepare_shutdown_disconnect();
        if (ble_ret != ESP_OK && ble_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "shutdown BLE disconnect preparation failed: %s", esp_err_to_name(ble_ret));
        }
    } else if (ble_hid_gap_set_low_power_advertising != NULL) {
        (void)ble_hid_gap_set_low_power_advertising(true);
    }

    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t hold_ret = board_set_power_hold_enabled(false);
    if (hold_ret != ESP_OK) {
        diag_log(
            DIAG_SRC_POWER,
            DIAG_POWER_HOLD_STATE,
            DIAG_SEV_ERROR,
            0u,
            (uint32_t)BOARD_PINS_PWR_HOLD_IO,
            2u,
            POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_DRIVE_HIGH);
        ESP_LOGE(TAG, "hardware shutdown failed: PWR_HOLD/GPIO9 drive-high ret=%s",
                 esp_err_to_name(hold_ret));
        power_manager_restore_after_shutdown_failure(reason, final_idle_ms, hold_ret);
        return hold_ret;
    }
    power_manager_log_power_hold_diag(
        DIAG_SEV_WARN,
        NULL,
        POWER_MANAGER_POWER_HOLD_ACTION_SHUTDOWN_DRIVE_HIGH);

    watchdog_platform_delay_ms(POWER_MANAGER_POWER_REMOVAL_WAIT_MS);

    ESP_LOGE(
        TAG,
        "hardware shutdown did not remove power after PWR_HOLD/GPIO9 drive-high within %u ms; restoring runtime low",
        (unsigned)POWER_MANAGER_POWER_REMOVAL_WAIT_MS);
    power_manager_restore_after_shutdown_failure(reason, final_idle_ms, ESP_FAIL);
    return ESP_FAIL;
}

static void power_manager_evaluate(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    uint64_t now_ms = power_manager_now_ms();
    power_manager_state_t previous = POWER_MANAGER_STATE_ACTIVE;
    power_manager_state_t next = POWER_MANAGER_STATE_ACTIVE;
    uint32_t user_idle_ms = 0;
    uint32_t radio_idle_ms = 0;
    uint32_t blockers = 0;
    uint32_t shutdown_blockers = 0;
    bool power_source_changed = false;
    bool automatic_shutdown_blocked = false;
    bool log_automatic_shutdown_blocked = false;
    power_manager_power_source_snapshot_t power_source = {0};
    power_manager_snapshot_t battery_snapshot = {0};
    bool refresh_battery = true;
    bool battery_refreshed = false;
    power_manager_read_power_source(&power_source);

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        refresh_battery =
            power_manager_should_refresh_battery_for_evaluate_locked(now_ms, &power_source);
        if (!refresh_battery &&
            !power_manager_copy_cached_battery_snapshot_locked(&battery_snapshot)) {
            refresh_battery = true;
        }
        xSemaphoreGive(s_mutex);
    }

    if (refresh_battery) {
        power_manager_update_battery_snapshot(&battery_snapshot);
        battery_refreshed = true;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    previous = s_state;
    if (battery_refreshed) {
        power_manager_store_battery_snapshot_locked(&battery_snapshot, now_ms);
    }
    power_manager_apply_charge_state_filter_locked(&power_source, now_ms, &battery_snapshot);
    power_source_changed = power_manager_sync_power_source_locked(&power_source, now_ms);
    bool ble_changed = power_manager_refresh_ble_connection_locked(now_ms);
    automatic_shutdown_blocked =
        power_manager_automatic_shutdown_blocked_by_external_power_locked(now_ms);
    next = power_manager_target_state_locked(now_ms);
    user_idle_ms = power_manager_user_idle_ms_locked(now_ms);
    radio_idle_ms = power_manager_radio_idle_ms_locked(now_ms);
    blockers = s_blockers;
    shutdown_blockers = power_manager_automatic_shutdown_blockers_for_source(blockers, &power_source);
    if (automatic_shutdown_blocked && !s_auto_shutdown_block_logged) {
        s_auto_shutdown_block_logged = true;
        log_automatic_shutdown_blocked = true;
    } else if (!automatic_shutdown_blocked) {
        s_auto_shutdown_block_logged = false;
    }
    if (previous != next) {
        s_state = next;
    }
    xSemaphoreGive(s_mutex);

    /* Low-battery protection: shut down immediately only on battery power.
       Active charging or debounced charge-full status blocks automatic
       low-battery shutdown even if the battery estimate is critical. */
    bool low_battery_shutdown_confirmed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        low_battery_shutdown_confirmed =
            power_manager_low_battery_shutdown_confirmed_locked(
                &battery_snapshot,
                &power_source,
                now_ms);
        xSemaphoreGive(s_mutex);
    }
    if (low_battery_shutdown_confirmed) {
        ESP_LOGW(
            TAG,
            "low battery critical shutdown: level=%u%% mv=%" PRIu32 " threshold=%u%%"
            " confirm_ms=%u usb_power=%u external_power=%u charging=%u charge_full=%u",
            battery_snapshot.battery_level_percent,
            battery_snapshot.battery_mv,
            (unsigned)POWER_MANAGER_BATTERY_CRITICAL_PERCENT,
            (unsigned)POWER_MANAGER_LOW_BATTERY_CONFIRM_MS,
            power_source.usb_power_present ? 1u : 0u,
            power_source.external_power_present ? 1u : 0u,
            power_source.charging ? 1u : 0u,
            power_source.charge_full ? 1u : 0u);
        esp_err_t lb_ret =
            power_manager_enter_hardware_shutdown(POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY);
        if (lb_ret != ESP_OK) {
            ESP_LOGW(TAG, "low battery shutdown rejected, retry is gated by shutdown-failure cooldown");
            if (lb_ret == ESP_ERR_INVALID_STATE || lb_ret == ESP_ERR_TIMEOUT) {
                power_manager_notify_power_led_error(false, "low_battery_shutdown_rejected");
            }
        }
        return;
    }

    if (power_source_changed) {
        ESP_LOGI(
            TAG,
            "power source changed: usb_det=%s bat_chg=%s bat_std=%s"
            " pwr_hold=%s usb_power_present=%u usb_serial_jtag_sof_active=%u"
            " charger_active=%u charge_power_present=%u"
            " charging=%u charge_full=%u external_power_present=%u usb_det_adc_valid=%u usb_det_adc_mv=%d usb_det_mismatch=%u"
            " usb_policy=%s charger_policy=%s pwr_hold_policy=%s",
            power_manager_gpio_level_name(power_source.usb_det_level),
            power_manager_gpio_level_name(power_source.bat_chg_level),
            power_manager_gpio_level_name(power_source.bat_std_level),
            power_manager_gpio_level_name(power_source.pwr_hold_level),
            power_source.usb_power_present ? 1u : 0u,
            power_source.usb_serial_jtag_sof_active ? 1u : 0u,
            power_source.charger_active ? 1u : 0u,
            power_source.charge_power_present ? 1u : 0u,
            power_source.charging ? 1u : 0u,
            power_source.charge_full ? 1u : 0u,
            power_source.external_power_present ? 1u : 0u,
            power_source.usb_det_adc_valid ? 1u : 0u,
            power_source.usb_det_adc_mv,
            power_source.usb_det_mismatch ? 1u : 0u,
            power_source.usb_det_policy != NULL ? power_source.usb_det_policy : "unknown",
            power_source.charger_polarity_policy != NULL
                ? power_source.charger_polarity_policy
                : "unknown",
            power_source.pwr_hold_policy != NULL
                ? power_source.pwr_hold_policy
                : "unknown");
        power_manager_log_power_source_diag(
            DIAG_SEV_INFO,
            &power_source,
            user_idle_ms,
            shutdown_blockers,
            automatic_shutdown_blocked);
        power_manager_log_power_transition_diag(&power_source, user_idle_ms);
    }
    if (ble_changed) {
        ESP_LOGI(TAG, "BLE connection state observed: connected=%u", s_ble_connected ? 1u : 0u);
    }
    ESP_LOGD(TAG, "idle clocks: user_idle_ms=%" PRIu32 " radio_idle_ms=%" PRIu32,
             user_idle_ms, radio_idle_ms);
    power_manager_log_transition(previous, next, user_idle_ms, blockers);
    power_manager_apply_state(previous, next);
    power_manager_apply_fast_idle_actions(next, user_idle_ms, blockers);
    power_manager_guard_runtime_power_hold_low(next);
    power_manager_update_power_input_irq_arm(next, &power_source);

    if (log_automatic_shutdown_blocked) {
        char shutdown_blocker_text[96];
        power_manager_blocker_names(
            shutdown_blockers,
            shutdown_blocker_text,
            sizeof(shutdown_blocker_text));
        ESP_LOGW(
            TAG,
            "automatic hardware shutdown blocked by external power"
            " shutdown_blockers=0x%08" PRIx32 " (%s) idle_ms=%" PRIu32
            " usb_det=%s bat_chg=%s bat_std=%s"
            " external_power_present=%u charging=%u charge_full=%u",
            shutdown_blockers,
            shutdown_blocker_text,
            user_idle_ms,
            power_manager_gpio_level_name(power_source.usb_det_level),
            power_manager_gpio_level_name(power_source.bat_chg_level),
            power_manager_gpio_level_name(power_source.bat_std_level),
            power_source.external_power_present ? 1u : 0u,
            power_source.charging ? 1u : 0u,
            power_source.charge_full ? 1u : 0u);
        diag_log(
            DIAG_SRC_POWER,
            DIAG_POWER_SLEEP_BLOCKED,
            DIAG_SEV_WARN,
            shutdown_blockers,
            user_idle_ms,
            (uint32_t)POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE,
            power_manager_encode_power_source_flags(&power_source, true));
        power_manager_log_power_source_diag(
            DIAG_SEV_WARN,
            &power_source,
            user_idle_ms,
            shutdown_blockers,
            true);
    }

    if (next == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN) {
        esp_err_t shutdown_ret =
            power_manager_enter_hardware_shutdown(POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE);
        if (shutdown_ret != ESP_OK) {
            ESP_LOGW(TAG, "automatic hardware shutdown failed, restore path handled retry policy");
        }
    }
}

static void power_manager_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("power_manager_task");

    while (1) {
        power_manager_state_t state = POWER_MANAGER_STATE_ACTIVE;
        if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            state = s_state;
            xSemaphoreGive(s_mutex);
        }

        if (state == POWER_MANAGER_STATE_ACTIVE) {
            (void)watchdog_platform_task_notify_take(
                pdTRUE,
                CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS);
        } else {
            (void)watchdog_platform_task_notify_take_low_power(
                pdTRUE,
                POWER_MANAGER_LOW_POWER_EVALUATE_INTERVAL_MS);
        }
        power_manager_evaluate();
        watchdog_platform_feed_current_task();
    }
}

esp_err_t power_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_last_user_activity_ms = power_manager_now_ms();
    s_last_radio_activity_ms = s_last_user_activity_ms;
    esp_err_t hold_ret = board_configure_power_hold_latch();
    s_initialized = true;

    esp_reset_reason_t reset_reason = esp_reset_reason();
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    ESP_LOGI(
        TAG,
        "power manager init: enabled=%u reset_reason=%u last_shutdown=%s"
        " last_shutdown_idle_ms=%" PRIu32 " last_shutdown_blockers=0x%08" PRIx32
        " pwr_hold_gpio=%d pwr_hold_level=%s pwr_hold_configured=%u pwr_hold_policy=%s"
        " voice_key_gpio=%u user_action=\"%s\"",
        CONFIG_POWER_MANAGER_ENABLE ? 1u : 0u,
        (unsigned)reset_reason,
        power_manager_shutdown_reason_name(s_last_shutdown_reason),
        s_last_shutdown_idle_ms,
        s_last_shutdown_blockers,
        power_hold.gpio,
        power_manager_gpio_level_name(power_hold.level),
        power_hold.configured ? 1u : 0u,
        power_hold.policy != NULL ? power_hold.policy : "unknown",
        (unsigned)BOARD_PINS_EC11_KEY_IO,
        POWER_MANAGER_SHUTDOWN_USER_ACTION);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_WAKE, DIAG_SEV_INFO,
             (uint32_t)reset_reason,
             power_hold.gpio >= 0 ? (uint32_t)power_hold.gpio : UINT32_MAX,
             (uint32_t)s_last_shutdown_reason,
             s_last_shutdown_idle_ms);
    power_manager_log_power_hold_diag(
        hold_ret == ESP_OK ? DIAG_SEV_INFO : DIAG_SEV_WARN,
        &power_hold,
        POWER_MANAGER_POWER_HOLD_ACTION_INIT);
    if (hold_ret != ESP_OK) {
        ESP_LOGW(TAG, "PWR_HOLD/GPIO9 runtime-low setup failed: %s", esp_err_to_name(hold_ret));
    }
    return ESP_OK;
}

esp_err_t power_manager_start(void)
{
    if (!CONFIG_POWER_MANAGER_ENABLE) {
        ESP_LOGW(TAG, "power manager disabled by config");
        return ESP_OK;
    }

    esp_err_t ret = power_manager_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_started) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreate(
        power_manager_task,
        "power_manager_task",
        POWER_MANAGER_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 6,
        &s_task_handle);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ret = power_manager_configure_power_input_wake();
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "power input wake degraded: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(
        TAG,
        "power manager started: audio_idle_ms=%u low_power_idle_ms=%u connected_idle_ms=%u disconnected_idle_ms=%u"
        " plugged_low_power_enabled=%u hardware_shutdown_ms=%u eval_ms=%u pwr_hold_gpio=%d",
        (unsigned)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS,
        (unsigned)power_manager_low_power_idle_ms(),
        (unsigned)power_manager_low_power_idle_ms(),
        (unsigned)power_manager_low_power_idle_ms(),
        power_manager_plugged_low_power_enabled() ? 1u : 0u,
        (unsigned)power_manager_hardware_shutdown_ms(),
        (unsigned)CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS,
        (int)BOARD_PINS_PWR_HOLD_IO);
    return ESP_OK;
}

void power_manager_record_activity(const char *reason)
{
    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    power_manager_state_t previous = POWER_MANAGER_STATE_ACTIVE;
    power_manager_state_t next = POWER_MANAGER_STATE_ACTIVE;
    uint32_t blockers = 0;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        previous = s_state;
        uint64_t now_ms = power_manager_now_ms();
        s_last_user_activity_ms = now_ms;
        s_last_radio_activity_ms = now_ms;
        s_auto_shutdown_block_logged = false;
        power_manager_clear_shutdown_failure_retry_locked();
        if (s_state != POWER_MANAGER_STATE_ACTIVE) {
            s_state = POWER_MANAGER_STATE_ACTIVE;
        }
        next = s_state;
        blockers = s_blockers;
        xSemaphoreGive(s_mutex);
    }

    if (previous != next) {
        ESP_LOGI(
            TAG,
            "activity reason=%s resumed from %s",
            reason != NULL ? reason : "unspecified",
            power_manager_state_name(previous));
        power_manager_log_transition(previous, next, 0, blockers);
        power_manager_apply_state(previous, next);
    } else {
        power_manager_set_audio_idle_power_save(false);
    }
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

void power_manager_set_blocker(uint32_t blocker_mask, bool enabled)
{
    if (!s_initialized || s_mutex == NULL || blocker_mask == 0) {
        return;
    }

    uint32_t old_blockers = 0;
    uint32_t new_blockers = 0;
    power_manager_state_t previous = POWER_MANAGER_STATE_ACTIVE;
    power_manager_state_t next = POWER_MANAGER_STATE_ACTIVE;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        old_blockers = s_blockers;
        if (enabled) {
            s_blockers |= blocker_mask;
        } else {
            s_blockers &= ~blocker_mask;
        }
        new_blockers = s_blockers;
        uint64_t now_ms = power_manager_now_ms();
        s_last_user_activity_ms = now_ms;
        s_last_radio_activity_ms = now_ms;
        s_auto_shutdown_block_logged = false;
        previous = s_state;
        if (power_manager_awake_blockers(s_blockers) != 0 &&
            s_state != POWER_MANAGER_STATE_ACTIVE) {
            s_state = POWER_MANAGER_STATE_ACTIVE;
        }
        next = s_state;
        xSemaphoreGive(s_mutex);
    }

    if (old_blockers != new_blockers) {
        char blocker_text[96];
        power_manager_blocker_names(new_blockers, blocker_text, sizeof(blocker_text));
        ESP_LOGI(
            TAG,
            "blocker %s mask=0x%08" PRIx32 " blockers=0x%08" PRIx32 " (%s)",
            enabled ? "set" : "clear",
            blocker_mask,
            new_blockers,
            blocker_text);
        diag_log(DIAG_SRC_POWER, DIAG_POWER_BLOCKER_CHANGE, DIAG_SEV_INFO,
                 old_blockers, new_blockers, blocker_mask, enabled ? 1 : 0);
    }

    if (previous != next) {
        power_manager_log_transition(previous, next, 0, new_blockers);
        power_manager_apply_state(previous, next);
    } else if (new_blockers != 0) {
        power_manager_set_audio_idle_power_save(false);
    }
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

void power_manager_set_ble_connected(bool connected)
{
    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    bool changed = false;
    power_manager_state_t previous = POWER_MANAGER_STATE_ACTIVE;
    power_manager_state_t next = POWER_MANAGER_STATE_ACTIVE;
    uint32_t blockers = 0;
    uint32_t user_idle_ms = 0;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        changed = s_ble_connected != connected;
        previous = s_state;
        uint64_t now_ms = power_manager_now_ms();
        if (changed) {
            power_manager_apply_ble_connection_change_locked(connected, now_ms);
        }
        user_idle_ms = power_manager_user_idle_ms_locked(now_ms);
        next = s_state;
        blockers = s_blockers;
        xSemaphoreGive(s_mutex);
    }

    if (changed) {
        ESP_LOGI(TAG, "BLE connection state changed: connected=%u", connected ? 1u : 0u);
    }
    if (previous != next) {
        power_manager_log_transition(previous, next, 0, blockers);
        power_manager_apply_state(previous, next);
    }
    power_manager_apply_fast_idle_actions(next, user_idle_ms, blockers);
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

static const char *power_manager_strip_prefix(const char *line)
{
    if (line == NULL) {
        return NULL;
    }
    if (*line == '~') {
        line++;
    }
    size_t prefix_len = strlen(POWER_MANAGER_USB_PREFIX);
    if (strncmp(line, POWER_MANAGER_USB_PREFIX, prefix_len) != 0) {
        return NULL;
    }
    return line + prefix_len;
}

static void power_manager_print_status(void)
{
    power_manager_snapshot_t snapshot;
    power_manager_get_snapshot(&snapshot);

    char blocker_text[96];
    power_manager_blocker_names(snapshot.blockers, blocker_text, sizeof(blocker_text));
    char shutdown_blocker_text[96];
    power_manager_blocker_names(
        snapshot.shutdown_blockers,
        shutdown_blocker_text,
        sizeof(shutdown_blocker_text));
    printf(
        "~POWER:STATUS state=%s blockers=0x%08" PRIx32 " blocker_names=%s"
        " shutdown_blockers=0x%08" PRIx32 " shutdown_blocker_names=%s idle_ms=%" PRIu32
        " user_idle_ms=%" PRIu32 " radio_idle_ms=%" PRIu32
        " ble_connected=%u automatic_shutdown_enabled=%u automatic_shutdown_blocked_by_external_power=%u"
        " external_power_present=%u usb_power_present=%u usb_serial_jtag_sof_active=%u"
        " charger_active=%u charge_power_present=%u"
        " charging=%u charge_full=%u"
        " charge_full_latched=%u charge_full_candidate_ms=%" PRIu32
        " charge_full_debounce_ms=%" PRIu32
        " charge_full_min_mv=%" PRIu32 " charge_full_min_percent=%u"
        " plugged_low_power_enabled=%u low_power_idle_allowed=%u"
        " power_input_wake_configured=%u power_input_irq_armed=%u"
        " usb_det_level=%s usb_det_adc_valid=%u usb_det_adc_mv=%d usb_det_mismatch=%u"
        " bat_chg_level=%s bat_std_level=%s pwr_hold_level=%s"
        " usb_det_policy=%s charger_polarity=%s pwr_hold_policy=%s"
        " battery_mv=%" PRIu32 " battery_level=%u battery_valid=%u"
        " last_shutdown_reason=%s last_shutdown_idle_ms=%" PRIu32
        " last_shutdown_blockers=0x%08" PRIx32
        " last_shutdown_failure_ret=%s shutdown_failure_retry_ms_left=%" PRIu32
        " shutdown_failure_retry_ms=%" PRIu32
        " guard=%u audio_idle_ms=%" PRIu32
        " audio_idle_power_save=%u audio_idle_blockers=0x%08" PRIx32
        " low_power_idle_ms=%" PRIu32
        " plugged_low_power_idle_ms=%" PRIu32 " battery_low_power_idle_ms=%" PRIu32
        " connected_idle_ms=%" PRIu32 " disconnected_idle_ms=%" PRIu32
        " hardware_shutdown_ms=%" PRIu32
        " plugged_auto_shutdown_ms=%" PRIu32 " battery_auto_shutdown_ms=%" PRIu32
        " pwr_hold_gpio=%d pwr_hold_level=%s pwr_hold_configured=%u pwr_hold_policy=%s"
        " voice_key_gpio=%" PRIu32 " hardware_shutdown_user_action=\"%s\"\n",
        power_manager_state_name(snapshot.state),
        snapshot.blockers,
        blocker_text,
        snapshot.shutdown_blockers,
        shutdown_blocker_text,
        snapshot.idle_ms,
        snapshot.user_idle_ms,
        snapshot.radio_idle_ms,
        snapshot.ble_connected ? 1u : 0u,
        power_manager_automatic_shutdown_enabled() ? 1u : 0u,
        snapshot.automatic_shutdown_blocked_by_external_power ? 1u : 0u,
        snapshot.external_power_present ? 1u : 0u,
        snapshot.usb_power_present ? 1u : 0u,
        snapshot.usb_serial_jtag_sof_active ? 1u : 0u,
        snapshot.charger_active ? 1u : 0u,
        snapshot.charge_power_present ? 1u : 0u,
        snapshot.charging ? 1u : 0u,
        snapshot.charge_full ? 1u : 0u,
        snapshot.charge_full_latched ? 1u : 0u,
        snapshot.charge_full_candidate_ms,
        snapshot.charge_full_debounce_ms,
        snapshot.charge_full_min_mv,
        snapshot.charge_full_min_percent,
        snapshot.plugged_low_power_enabled ? 1u : 0u,
        snapshot.low_power_idle_allowed ? 1u : 0u,
        snapshot.power_input_wake_configured ? 1u : 0u,
        snapshot.power_input_irq_armed ? 1u : 0u,
        power_manager_gpio_level_name(snapshot.usb_det_level),
        snapshot.usb_det_adc_valid ? 1u : 0u,
        snapshot.usb_det_adc_mv,
        snapshot.usb_det_mismatch ? 1u : 0u,
        power_manager_gpio_level_name(snapshot.bat_chg_level),
        power_manager_gpio_level_name(snapshot.bat_std_level),
        power_manager_gpio_level_name(snapshot.pwr_hold_level),
        snapshot.usb_det_policy != NULL ? snapshot.usb_det_policy : "unknown",
        snapshot.charger_polarity_policy != NULL ? snapshot.charger_polarity_policy : "unknown",
        snapshot.pwr_hold_policy != NULL ? snapshot.pwr_hold_policy : "unknown",
        snapshot.battery_mv,
        snapshot.battery_level_percent,
        snapshot.battery_valid ? 1u : 0u,
        power_manager_shutdown_reason_name(snapshot.last_shutdown_reason),
        snapshot.last_shutdown_idle_ms,
        snapshot.last_shutdown_blockers,
        esp_err_to_name(snapshot.last_shutdown_failure_ret),
        snapshot.shutdown_failure_retry_ms_left,
        snapshot.shutdown_failure_retry_ms,
        snapshot.hardware_shutdown_guard_enabled ? 1u : 0u,
        snapshot.audio_idle_threshold_ms,
        snapshot.audio_idle_power_save_enabled ? 1u : 0u,
        snapshot.audio_idle_blockers,
        snapshot.low_power_idle_threshold_ms,
        snapshot.plugged_low_power_idle_threshold_ms,
        snapshot.battery_low_power_idle_threshold_ms,
        snapshot.connected_idle_threshold_ms,
        snapshot.disconnected_idle_threshold_ms,
        snapshot.hardware_shutdown_threshold_ms,
        snapshot.plugged_auto_shutdown_threshold_ms,
        snapshot.battery_auto_shutdown_threshold_ms,
        snapshot.pwr_hold_gpio,
        power_manager_gpio_level_name(snapshot.pwr_hold_level),
        snapshot.pwr_hold_configured ? 1u : 0u,
        snapshot.pwr_hold_policy != NULL ? snapshot.pwr_hold_policy : "unknown",
        snapshot.voice_key_gpio,
        snapshot.hardware_shutdown_user_action != NULL
            ? snapshot.hardware_shutdown_user_action
            : "short-press hardware power key");
    fflush(stdout);
}

static void power_manager_print_pm_locks(void)
{
#if CONFIG_PM_ENABLE
    printf("~POWER:PM begin pm_enable=1 profiling=%u\n",
#if CONFIG_PM_PROFILING
           1u
#else
           0u
#endif
    );
    esp_err_t ret = esp_pm_dump_locks(stdout);
    printf("~POWER:PM end result=%s\n", esp_err_to_name(ret));
#else
    printf("~POWER:PM result=ESP_ERR_NOT_SUPPORTED pm_enable=0\n");
#endif
    fflush(stdout);
}

bool power_manager_consume_usb_command(const char *line)
{
    const char *command = power_manager_strip_prefix(line);
    if (command == NULL) {
        return false;
    }

    if (strcmp(command, "STATUS") == 0) {
        power_manager_print_status();
        return true;
    }

    if (strcmp(command, "PM") == 0 || strcmp(command, "PM:LOCKS") == 0) {
        power_manager_print_pm_locks();
        return true;
    }

    if (strcmp(command, "SHUTDOWN") == 0 ||
        strcmp(command, "TEST:SHUTDOWN") == 0 ||
        strcmp(command, "TEST-SHUTDOWN") == 0) {
        power_manager_record_activity("usb_power_command");
        printf("~POWER:SHUTDOWN result=accepted trigger=serial_manual command=%s\n", command);
        fflush(stdout);
        (void)power_manager_enter_hardware_shutdown(POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND);
        return true;
    }

    if (strcmp(command, "ACTIVITY") == 0) {
        power_manager_record_activity("usb_power_command");
        power_manager_record_activity("manual_activity");
        ESP_LOGI(TAG, "manual activity accepted");
        return true;
    }

    ESP_LOGW(TAG, "POWER: unknown command: %s", command);
    return true;
}
