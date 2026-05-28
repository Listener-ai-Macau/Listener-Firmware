#include "power_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "battery_monitor.h"
#include "board_pins.h"
#include "diag_log.h"
#include "watchdog_platform.h"

extern esp_err_t audio_capture_set_idle_power_save(bool enabled) __attribute__((weak));
extern bool audio_capture_session_is_active(void) __attribute__((weak));
extern bool ble_hid_gap_is_connected(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_set_low_power_advertising(bool enabled) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_low_power_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern void system_health_set_low_power_mode(bool enabled) __attribute__((weak));

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

#ifndef CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS
#define CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS 1800000
#endif

#ifndef CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS
#define CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS 2000
#endif

#define POWER_MANAGER_USB_PREFIX "POWER:"
#define POWER_MANAGER_BATTERY_WARN_PERCENT 10U
#define POWER_MANAGER_TASK_STACK_BYTES (4 * 1024)
#define POWER_MANAGER_WAKE_CAPABLE_KEYS "KEY4/GPIO21"
#define POWER_MANAGER_VOICE_KEY_LIMITATION "GPIO35 voice key is not RTC deep-sleep wake capable on V1"
#define POWER_MANAGER_WAKE_USER_ACTION "press KEY4/GPIO21 after deep sleep"
#define POWER_MANAGER_WAKE_POLICY_CODE ((uint32_t)POWER_MANAGER_WAKE_POLICY_KEY4_ONLY)
#define POWER_MANAGER_WAKE_VOICE_KEY_CAPABLE_CODE 0U

static const char *TAG = "power_manager";

RTC_DATA_ATTR static uint32_t s_rtc_last_sleep_reason;
RTC_DATA_ATTR static uint32_t s_rtc_last_idle_ms;
RTC_DATA_ATTR static uint32_t s_rtc_last_blockers;
RTC_DATA_ATTR static uint32_t s_rtc_sleep_count;

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task_handle;
static bool s_initialized;
static bool s_started;
static bool s_ble_connected;
static bool s_battery_warning_logged;
static bool s_audio_idle_power_save_enabled;
static uint32_t s_blockers;
static uint32_t s_last_activity_ms;
static power_manager_state_t s_state = POWER_MANAGER_STATE_ACTIVE;
static power_manager_wake_source_t s_last_wake_source = POWER_MANAGER_WAKE_SOURCE_POWER_ON;

static uint32_t power_manager_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
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
    case POWER_MANAGER_STATE_OVERNIGHT_SLEEP:
        return "OVERNIGHT_SLEEP";
    default:
        return "UNKNOWN";
    }
}

const char *power_manager_sleep_reason_name(power_manager_sleep_reason_t reason)
{
    switch (reason) {
    case POWER_MANAGER_SLEEP_REASON_NONE:
        return "none";
    case POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE:
        return "overnight_idle";
    case POWER_MANAGER_SLEEP_REASON_MANUAL_COMMAND:
        return "manual_command";
    default:
        return "unknown";
    }
}

const char *power_manager_wake_source_name(power_manager_wake_source_t source)
{
    switch (source) {
    case POWER_MANAGER_WAKE_SOURCE_UNDEFINED:
        return "undefined";
    case POWER_MANAGER_WAKE_SOURCE_EXT0:
        return "ext0";
    case POWER_MANAGER_WAKE_SOURCE_EXT1:
        return "ext1";
    case POWER_MANAGER_WAKE_SOURCE_TIMER:
        return "timer";
    case POWER_MANAGER_WAKE_SOURCE_TOUCHPAD:
        return "touchpad";
    case POWER_MANAGER_WAKE_SOURCE_ULP:
        return "ulp";
    case POWER_MANAGER_WAKE_SOURCE_GPIO:
        return "gpio";
    case POWER_MANAGER_WAKE_SOURCE_UART:
        return "uart";
    case POWER_MANAGER_WAKE_SOURCE_POWER_ON:
        return "power_on";
    default:
        return "unknown";
    }
}

const char *power_manager_wake_policy_name(power_manager_wake_policy_t policy)
{
    switch (policy) {
    case POWER_MANAGER_WAKE_POLICY_KEY4_ONLY:
        return "key4_only";
    default:
        return "unknown";
    }
}

static power_manager_wake_source_t power_manager_map_wakeup(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
        return POWER_MANAGER_WAKE_SOURCE_EXT0;
    case ESP_SLEEP_WAKEUP_EXT1:
        return POWER_MANAGER_WAKE_SOURCE_EXT1;
    case ESP_SLEEP_WAKEUP_TIMER:
        return POWER_MANAGER_WAKE_SOURCE_TIMER;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return POWER_MANAGER_WAKE_SOURCE_TOUCHPAD;
    case ESP_SLEEP_WAKEUP_ULP:
        return POWER_MANAGER_WAKE_SOURCE_ULP;
    case ESP_SLEEP_WAKEUP_GPIO:
        return POWER_MANAGER_WAKE_SOURCE_GPIO;
    case ESP_SLEEP_WAKEUP_UART:
        return POWER_MANAGER_WAKE_SOURCE_UART;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        return s_rtc_sleep_count > 0 ? POWER_MANAGER_WAKE_SOURCE_UNDEFINED : POWER_MANAGER_WAKE_SOURCE_POWER_ON;
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
    };

    bool first = true;
    for (size_t index = 0; index < sizeof(entries) / sizeof(entries[0]); ++index) {
        if ((blockers & entries[index].mask) == 0) {
            continue;
        }

        size_t used = strlen(buffer);
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

static uint64_t power_manager_wake_gpio_mask(void)
{
    return (BOARD_PINS_KEY4_IO != GPIO_NUM_NC &&
            BOARD_PINS_KEY4_IO >= 0 &&
            BOARD_PINS_KEY4_IO < 64 &&
            rtc_gpio_is_valid_gpio(BOARD_PINS_KEY4_IO))
        ? (1ULL << (uint32_t)BOARD_PINS_KEY4_IO)
        : 0;
}

static bool power_manager_voice_key_rtc_capable(void)
{
    return BOARD_PINS_EC11_KEY_IO != GPIO_NUM_NC &&
           BOARD_PINS_EC11_KEY_IO >= 0 &&
           BOARD_PINS_EC11_KEY_IO < 64 &&
           rtc_gpio_is_valid_gpio(BOARD_PINS_EC11_KEY_IO);
}

static void power_manager_log_wake_policy(uint64_t wake_gpio_mask)
{
    diag_log(DIAG_SRC_POWER, DIAG_POWER_WAKE_POLICY, DIAG_SEV_INFO,
             POWER_MANAGER_WAKE_POLICY_CODE,
             (uint32_t)(wake_gpio_mask & 0xffffffffu),
             POWER_MANAGER_WAKE_VOICE_KEY_CAPABLE_CODE,
             (uint32_t)BOARD_PINS_EC11_KEY_IO);
}

static uint32_t power_manager_idle_ms_locked(uint32_t now_ms)
{
    return now_ms - s_last_activity_ms;
}

static power_manager_state_t power_manager_target_state_locked(uint32_t now_ms)
{
    uint32_t idle_ms = power_manager_idle_ms_locked(now_ms);
    if (s_blockers != 0) {
        return POWER_MANAGER_STATE_ACTIVE;
    }

    if (CONFIG_POWER_MANAGER_ENABLE &&
        idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS) {
        return POWER_MANAGER_STATE_OVERNIGHT_SLEEP;
    }

    if (s_ble_connected) {
        return idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS
            ? POWER_MANAGER_STATE_CONNECTED_IDLE
            : POWER_MANAGER_STATE_ACTIVE;
    }

    return idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS
        ? POWER_MANAGER_STATE_DISCONNECTED_IDLE
        : POWER_MANAGER_STATE_ACTIVE;
}

static bool power_manager_refresh_ble_connection_locked(uint32_t now_ms)
{
    bool connected = ble_hid_gap_is_connected != NULL && ble_hid_gap_is_connected();
    if (s_ble_connected == connected) {
        return false;
    }

    s_ble_connected = connected;
    s_last_activity_ms = now_ms;
    s_state = POWER_MANAGER_STATE_ACTIVE;
    return true;
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

    switch (next) {
    case POWER_MANAGER_STATE_ACTIVE:
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
        power_manager_set_audio_idle_power_save(true);
        if (system_health_set_low_power_mode != NULL) {
            system_health_set_low_power_mode(true);
        }
        if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(true);
        }
        break;
    case POWER_MANAGER_STATE_OVERNIGHT_SLEEP:
        power_manager_set_audio_idle_power_save(true);
        if (system_health_set_low_power_mode != NULL) {
            system_health_set_low_power_mode(true);
        }
        if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(true);
        }
        break;
    default:
        break;
    }
}

static void power_manager_apply_fast_idle_actions(power_manager_state_t state, uint32_t idle_ms, uint32_t blockers)
{
    if (state != POWER_MANAGER_STATE_ACTIVE) {
        return;
    }

    bool audio_idle = blockers == 0 &&
        idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
    power_manager_set_audio_idle_power_save(audio_idle);
}

static void power_manager_update_battery_snapshot(power_manager_snapshot_t *snapshot)
{
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

void power_manager_get_snapshot(power_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    uint32_t now_ms = power_manager_now_ms();

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot->state = s_state;
        snapshot->blockers = s_blockers;
        snapshot->idle_ms = power_manager_idle_ms_locked(now_ms);
        snapshot->ble_connected = s_ble_connected;
        snapshot->last_sleep_reason = (power_manager_sleep_reason_t)s_rtc_last_sleep_reason;
        snapshot->last_wake_source = s_last_wake_source;
        xSemaphoreGive(s_mutex);
    }

    snapshot->connected_idle_threshold_ms = CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS;
    snapshot->audio_idle_threshold_ms = CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
    snapshot->disconnected_idle_threshold_ms = CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS;
    snapshot->overnight_sleep_threshold_ms = CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS;
    snapshot->overnight_guard_enabled = CONFIG_POWER_MANAGER_ENABLE != 0;
    snapshot->wake_gpio_mask = power_manager_wake_gpio_mask();
    snapshot->wake_policy = POWER_MANAGER_WAKE_POLICY_KEY4_ONLY;
    snapshot->wake_key_gpio = (uint32_t)BOARD_PINS_KEY4_IO;
    snapshot->wake_key_rtc_capable = snapshot->wake_gpio_mask != 0;
    snapshot->voice_key_gpio = (uint32_t)BOARD_PINS_EC11_KEY_IO;
    snapshot->voice_key_rtc_capable = power_manager_voice_key_rtc_capable();
    snapshot->voice_key_deep_sleep_wake_enabled = false;
    snapshot->wake_capable_keys = POWER_MANAGER_WAKE_CAPABLE_KEYS;
    snapshot->voice_key_limitation = POWER_MANAGER_VOICE_KEY_LIMITATION;
    snapshot->wake_user_action = POWER_MANAGER_WAKE_USER_ACTION;
    power_manager_update_battery_snapshot(snapshot);
}

static esp_err_t power_manager_configure_wakeup(uint64_t wake_gpio_mask)
{
    if (wake_gpio_mask == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t clear_ret = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (clear_ret != ESP_OK) {
        return clear_ret;
    }

    gpio_config_t input_config = {
        .pin_bit_mask = wake_gpio_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&input_config);
    if (ret != ESP_OK) {
        return ret;
    }

    const gpio_num_t candidates[] = {
        BOARD_PINS_KEY4_IO,
    };
    for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        gpio_num_t gpio = candidates[index];
        if (gpio == GPIO_NUM_NC || gpio < 0 || gpio >= 64) {
            continue;
        }
        if ((wake_gpio_mask & (1ULL << (uint32_t)gpio)) == 0) {
            continue;
        }
        (void)rtc_gpio_pullup_en(gpio);
        (void)rtc_gpio_pulldown_dis(gpio);
    }

    return esp_sleep_enable_ext1_wakeup_io(wake_gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
}

static esp_err_t power_manager_enter_sleep(power_manager_sleep_reason_t reason)
{
    if (!CONFIG_POWER_MANAGER_ENABLE) {
        ESP_LOGW(TAG, "sleep rejected: power manager disabled");
        return ESP_ERR_INVALID_STATE;
    }

    power_manager_snapshot_t snapshot;
    power_manager_get_snapshot(&snapshot);
    if (snapshot.blockers != 0) {
        char blocker_text[96];
        power_manager_blocker_names(snapshot.blockers, blocker_text, sizeof(blocker_text));
        ESP_LOGW(
            TAG,
            "sleep rejected: blockers=0x%08" PRIx32 " (%s) idle_ms=%" PRIu32
            " wake_policy=%s wake_keys=%s voice_key_wake=%u limitation=\"%s\"",
            snapshot.blockers,
            blocker_text,
            snapshot.idle_ms,
            power_manager_wake_policy_name(snapshot.wake_policy),
            snapshot.wake_capable_keys,
            snapshot.voice_key_deep_sleep_wake_enabled ? 1u : 0u,
            snapshot.voice_key_limitation);
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 snapshot.blockers, snapshot.idle_ms, (uint32_t)reason, 0);
        power_manager_log_wake_policy(snapshot.wake_gpio_mask);
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_capture_session_is_active != NULL && audio_capture_session_is_active()) {
        ESP_LOGW(TAG, "sleep rejected: audio capture session active");
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 POWER_MANAGER_BLOCKER_RECORDING, snapshot.idle_ms, (uint32_t)reason, 0);
        power_manager_log_wake_policy(snapshot.wake_gpio_mask);
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t wake_gpio_mask = power_manager_wake_gpio_mask();
    esp_err_t wake_ret = power_manager_configure_wakeup(wake_gpio_mask);
    if (wake_ret != ESP_OK) {
        ESP_LOGW(TAG, "sleep rejected: wake GPIO unavailable ret=%s mask=0x%016" PRIx64,
                 esp_err_to_name(wake_ret), wake_gpio_mask);
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 0, snapshot.idle_ms, (uint32_t)reason, (uint32_t)wake_ret);
        power_manager_log_wake_policy(wake_gpio_mask);
        return wake_ret;
    }

    s_rtc_last_sleep_reason = (uint32_t)reason;
    s_rtc_last_idle_ms = snapshot.idle_ms;
    s_rtc_last_blockers = snapshot.blockers;
    s_rtc_sleep_count++;

    diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_ENTRY, DIAG_SEV_INFO,
             snapshot.idle_ms,
             snapshot.battery_valid ? snapshot.battery_mv : 0,
             snapshot.battery_valid ? snapshot.battery_level_percent : 0xFF,
             (uint32_t)reason);
    power_manager_log_wake_policy(wake_gpio_mask);
    ESP_LOGW(
        TAG,
        "entering deep sleep reason=%s idle_ms=%" PRIu32 " battery_mv=%" PRIu32
        " level=%u wake_mask=0x%016" PRIx64 " wake_policy=%s wake_keys=%s"
        " voice_key_wake=%u limitation=\"%s\" user_action=\"%s\"",
        power_manager_sleep_reason_name(reason),
        snapshot.idle_ms,
        snapshot.battery_mv,
        snapshot.battery_level_percent,
        wake_gpio_mask,
        power_manager_wake_policy_name(snapshot.wake_policy),
        snapshot.wake_capable_keys,
        snapshot.voice_key_deep_sleep_wake_enabled ? 1u : 0u,
        snapshot.voice_key_limitation,
        snapshot.wake_user_action);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
    return ESP_OK;
}

static void power_manager_evaluate(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    uint32_t now_ms = power_manager_now_ms();
    power_manager_state_t previous = POWER_MANAGER_STATE_ACTIVE;
    power_manager_state_t next = POWER_MANAGER_STATE_ACTIVE;
    uint32_t idle_ms = 0;
    uint32_t blockers = 0;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    previous = s_state;
    bool ble_changed = power_manager_refresh_ble_connection_locked(now_ms);
    next = power_manager_target_state_locked(now_ms);
    idle_ms = power_manager_idle_ms_locked(now_ms);
    blockers = s_blockers;
    if (previous != next) {
        s_state = next;
    }
    xSemaphoreGive(s_mutex);

    if (ble_changed) {
        ESP_LOGI(TAG, "BLE connection state observed: connected=%u", s_ble_connected ? 1u : 0u);
    }
    power_manager_log_transition(previous, next, idle_ms, blockers);
    power_manager_apply_state(previous, next);
    power_manager_apply_fast_idle_actions(next, idle_ms, blockers);

    if (next == POWER_MANAGER_STATE_OVERNIGHT_SLEEP) {
        esp_err_t sleep_ret = power_manager_enter_sleep(POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE);
        if (sleep_ret != ESP_OK && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_last_activity_ms = power_manager_now_ms();
            s_state = s_ble_connected
                ? POWER_MANAGER_STATE_CONNECTED_IDLE
                : POWER_MANAGER_STATE_DISCONNECTED_IDLE;
            xSemaphoreGive(s_mutex);
        }
    }
}

static void power_manager_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("power_manager_task");

    while (1) {
        watchdog_platform_delay_ms(CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS);
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

    s_last_activity_ms = power_manager_now_ms();
    s_last_wake_source = power_manager_map_wakeup(esp_sleep_get_wakeup_cause());
    s_initialized = true;

    uint64_t ext1_status = esp_sleep_get_ext1_wakeup_status();
    uint64_t wake_gpio_mask = power_manager_wake_gpio_mask();
    ESP_LOGI(
        TAG,
        "power manager init: enabled=%u wake=%s ext1=0x%016" PRIx64
        " last_sleep=%s last_idle_ms=%" PRIu32 " last_blockers=0x%08" PRIx32
        " wake_policy=%s wake_keys=%s voice_key_gpio=%u voice_key_wake=0 limitation=\"%s\"",
        CONFIG_POWER_MANAGER_ENABLE ? 1u : 0u,
        power_manager_wake_source_name(s_last_wake_source),
        ext1_status,
        power_manager_sleep_reason_name((power_manager_sleep_reason_t)s_rtc_last_sleep_reason),
        s_rtc_last_idle_ms,
        s_rtc_last_blockers,
        power_manager_wake_policy_name(POWER_MANAGER_WAKE_POLICY_KEY4_ONLY),
        POWER_MANAGER_WAKE_CAPABLE_KEYS,
        (unsigned)BOARD_PINS_EC11_KEY_IO,
        POWER_MANAGER_VOICE_KEY_LIMITATION);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_WAKE, DIAG_SEV_INFO,
             (uint32_t)s_last_wake_source,
             (uint32_t)(ext1_status & 0xffffffffu),
             s_rtc_last_sleep_reason,
             s_rtc_last_idle_ms);
    power_manager_log_wake_policy(wake_gpio_mask);
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
    ESP_LOGI(
        TAG,
        "power manager started: audio_idle_ms=%u connected_idle_ms=%u disconnected_idle_ms=%u"
        " overnight_sleep_ms=%u eval_ms=%u wake_mask=0x%016" PRIx64
        " wake_policy=%s wake_keys=%s voice_key_wake=0",
        (unsigned)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS,
        (unsigned)CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS,
        (unsigned)CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS,
        (unsigned)CONFIG_POWER_MANAGER_OVERNIGHT_SLEEP_MS,
        (unsigned)CONFIG_POWER_MANAGER_EVALUATE_INTERVAL_MS,
        power_manager_wake_gpio_mask(),
        power_manager_wake_policy_name(POWER_MANAGER_WAKE_POLICY_KEY4_ONLY),
        POWER_MANAGER_WAKE_CAPABLE_KEYS);
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
        s_last_activity_ms = power_manager_now_ms();
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
        s_last_activity_ms = power_manager_now_ms();
        previous = s_state;
        if (s_blockers != 0 && s_state != POWER_MANAGER_STATE_ACTIVE) {
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
}

void power_manager_set_ble_connected(bool connected)
{
    if (!s_initialized || s_mutex == NULL) {
        return;
    }

    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        changed = s_ble_connected != connected;
        s_ble_connected = connected;
        s_last_activity_ms = power_manager_now_ms();
        s_state = POWER_MANAGER_STATE_ACTIVE;
        xSemaphoreGive(s_mutex);
    }

    if (changed) {
        ESP_LOGI(TAG, "BLE connection state changed: connected=%u", connected ? 1u : 0u);
    }
    power_manager_apply_state(POWER_MANAGER_STATE_CONNECTED_IDLE, POWER_MANAGER_STATE_ACTIVE);
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
    printf(
        "~POWER:STATUS state=%s blockers=0x%08" PRIx32 " blocker_names=%s idle_ms=%" PRIu32
        " ble_connected=%u battery_mv=%" PRIu32 " battery_level=%u battery_valid=%u"
        " last_sleep_reason=%s last_wake_source=%s guard=%u audio_idle_ms=%" PRIu32
        " connected_idle_ms=%" PRIu32
        " disconnected_idle_ms=%" PRIu32 " overnight_sleep_ms=%" PRIu32
        " wake_policy=%s wake_gpio_mask=0x%016" PRIx64 " wake_capable_keys=%s"
        " wake_key_gpio=%" PRIu32 " wake_key_rtc_capable=%u"
        " voice_key_gpio=%" PRIu32 " voice_key_rtc_capable=%u"
        " voice_key_deep_sleep_wake=%u voice_key_limitation=\"%s\" wake_user_action=\"%s\"\n",
        power_manager_state_name(snapshot.state),
        snapshot.blockers,
        blocker_text,
        snapshot.idle_ms,
        snapshot.ble_connected ? 1u : 0u,
        snapshot.battery_mv,
        snapshot.battery_level_percent,
        snapshot.battery_valid ? 1u : 0u,
        power_manager_sleep_reason_name(snapshot.last_sleep_reason),
        power_manager_wake_source_name(snapshot.last_wake_source),
        snapshot.overnight_guard_enabled ? 1u : 0u,
        snapshot.audio_idle_threshold_ms,
        snapshot.connected_idle_threshold_ms,
        snapshot.disconnected_idle_threshold_ms,
        snapshot.overnight_sleep_threshold_ms,
        power_manager_wake_policy_name(snapshot.wake_policy),
        snapshot.wake_gpio_mask,
        snapshot.wake_capable_keys,
        snapshot.wake_key_gpio,
        snapshot.wake_key_rtc_capable ? 1u : 0u,
        snapshot.voice_key_gpio,
        snapshot.voice_key_rtc_capable ? 1u : 0u,
        snapshot.voice_key_deep_sleep_wake_enabled ? 1u : 0u,
        snapshot.voice_key_limitation,
        snapshot.wake_user_action);
    fflush(stdout);
}

bool power_manager_consume_usb_command(const char *line)
{
    const char *command = power_manager_strip_prefix(line);
    if (command == NULL) {
        return false;
    }

    power_manager_record_activity("usb_power_command");

    if (strcmp(command, "STATUS") == 0) {
        power_manager_print_status();
        return true;
    }

    if (strcmp(command, "SLEEP") == 0) {
        (void)power_manager_enter_sleep(POWER_MANAGER_SLEEP_REASON_MANUAL_COMMAND);
        return true;
    }

    if (strcmp(command, "ACTIVITY") == 0) {
        power_manager_record_activity("manual_activity");
        ESP_LOGI(TAG, "manual activity accepted");
        return true;
    }

    ESP_LOGW(TAG, "POWER: unknown command: %s", command);
    return true;
}
