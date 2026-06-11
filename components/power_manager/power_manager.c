#include "power_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
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
extern esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_low_power_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_request_reconnect(void) __attribute__((weak));
extern void system_health_set_low_power_mode(bool enabled) __attribute__((weak));
extern void status_led_set_low_power_disabled(bool disabled) __attribute__((weak));
extern void status_led_prepare_sleep(void) __attribute__((weak));

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
#ifndef CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT
#define CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT 0
#endif
#define POWER_MANAGER_BATTERY_CRITICAL_PERCENT ((uint8_t)CONFIG_POWER_MANAGER_BATTERY_CRITICAL_PERCENT)
#define POWER_MANAGER_TASK_STACK_BYTES (4 * 1024)
#define POWER_MANAGER_SHUTDOWN_USER_ACTION "short-press hardware power key for cold boot after PWR_HOLD/GPIO11 drive-high shutdown"
#define POWER_MANAGER_POWER_SOURCE_USB_PRESENT (1u << 0)
#define POWER_MANAGER_POWER_SOURCE_CHARGING (1u << 1)
#define POWER_MANAGER_POWER_SOURCE_CHARGE_FULL (1u << 2)
#define POWER_MANAGER_POWER_SOURCE_EXTERNAL_PRESENT (1u << 3)
#define POWER_MANAGER_POWER_SOURCE_AUTO_SHUTDOWN_BLOCKED (1u << 4)

static const char *TAG = "power_manager";

typedef struct {
    int usb_det_level;
    int bat_chg_level;
    int bat_std_level;
    int pwr_hold_level;
    bool usb_power_present;
    bool external_power_present;
    bool charging;
    bool charge_full;
    const char *usb_det_policy;
    const char *charger_polarity_policy;
    const char *pwr_hold_policy;
} power_manager_power_source_snapshot_t;

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task_handle;
static bool s_initialized;
static bool s_started;
static bool s_ble_connected;
static bool s_battery_warning_logged;
static bool s_audio_idle_power_save_enabled;
static bool s_power_source_initialized;
static bool s_usb_power_present;
static bool s_external_power_present;
static bool s_charging;
static bool s_charge_full;
static bool s_auto_shutdown_block_logged;
static int s_usb_det_level = -1;
static int s_bat_chg_level = -1;
static int s_bat_std_level = -1;
static int s_pwr_hold_level = -1;
static uint32_t s_blockers;
static uint64_t s_last_user_activity_ms;
static uint64_t s_last_radio_activity_ms;
static power_manager_shutdown_reason_t s_last_shutdown_reason =
    POWER_MANAGER_SHUTDOWN_REASON_NONE;
static uint32_t s_last_shutdown_idle_ms;
static uint32_t s_last_shutdown_blockers;
static power_manager_state_t s_state = POWER_MANAGER_STATE_ACTIVE;

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
    return device_settings_get_battery_auto_shutdown_ms();
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
        return "unknown";
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

static void power_manager_read_power_source(power_manager_power_source_snapshot_t *out_source)
{
    if (out_source == NULL) {
        return;
    }

    board_v2_power_input_snapshot_t board_snapshot = {0};
    board_get_v2_power_input_snapshot(&board_snapshot);

    *out_source = (power_manager_power_source_snapshot_t){
        .usb_det_level = board_snapshot.usb_det_level,
        .bat_chg_level = board_snapshot.bat_chg_level,
        .bat_std_level = board_snapshot.bat_std_level,
        .pwr_hold_level = board_snapshot.pwr_hold_level,
        .usb_power_present = board_snapshot.usb_det_level > 0,
        .charging = board_snapshot.bat_chg_level == 0,
        .charge_full = board_snapshot.bat_std_level == 0,
        .usb_det_policy = board_snapshot.usb_det_policy,
        .charger_polarity_policy = board_snapshot.charger_polarity_policy,
        .pwr_hold_policy = board_snapshot.pwr_hold_policy,
    };
    /*
     * CHG/STD are charger status outputs. They remain useful diagnostics, but
     * on a battery-only full pack they must not keep long-idle hardware
     * shutdown blocked after VBUS/USB_DET is gone.
     */
    out_source->external_power_present = out_source->usb_power_present;
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
        shutdown_blockers |= POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
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
        source->pwr_hold_level > 0 ? 1u : 0u,
        (uint32_t)BOARD_PINS_PWR_HOLD_IO,
        power_manager_encode_gpio_level(source->pwr_hold_level),
        0u);
}

static uint32_t power_manager_user_idle_ms_locked(uint64_t now_ms)
{
    return power_manager_clamp_u64_to_u32(now_ms - s_last_user_activity_ms);
}

static uint32_t power_manager_radio_idle_ms_locked(uint64_t now_ms)
{
    return power_manager_clamp_u64_to_u32(now_ms - s_last_radio_activity_ms);
}

static bool power_manager_sync_power_source_locked(
    const power_manager_power_source_snapshot_t *source,
    uint64_t now_ms)
{
    if (source == NULL) {
        return false;
    }

    bool changed = !s_power_source_initialized ||
                   s_usb_det_level != source->usb_det_level ||
                   s_bat_chg_level != source->bat_chg_level ||
                   s_bat_std_level != source->bat_std_level ||
                   s_pwr_hold_level != source->pwr_hold_level ||
                   s_usb_power_present != source->usb_power_present ||
                   s_external_power_present != source->external_power_present ||
                   s_charging != source->charging ||
                   s_charge_full != source->charge_full;
    bool external_changed = !s_power_source_initialized ||
                            s_external_power_present != source->external_power_present;

    if (!changed) {
        return false;
    }

    if (s_power_source_initialized && external_changed) {
        s_last_user_activity_ms = now_ms;
        s_last_radio_activity_ms = now_ms;
    }

    s_power_source_initialized = true;
    s_usb_det_level = source->usb_det_level;
    s_bat_chg_level = source->bat_chg_level;
    s_bat_std_level = source->bat_std_level;
    s_pwr_hold_level = source->pwr_hold_level;
    s_usb_power_present = source->usb_power_present;
    s_external_power_present = source->external_power_present;
    s_charging = source->charging;
    s_charge_full = source->charge_full;
    s_auto_shutdown_block_logged = false;

    /* External power keeps the product fully awake while plugged, while
       automatic-shutdown diagnostics still report it as the shutdown cause. */
    if (source->external_power_present) {
        s_blockers |= POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
    } else {
        s_blockers &= ~(uint32_t)POWER_MANAGER_BLOCKER_EXTERNAL_POWER;
    }
    return true;
}

static power_manager_state_t power_manager_awake_idle_state_locked(uint32_t radio_idle_ms)
{
    if (s_ble_connected) {
        return radio_idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS
            ? POWER_MANAGER_STATE_CONNECTED_IDLE
            : POWER_MANAGER_STATE_ACTIVE;
    }

    return radio_idle_ms >= (uint32_t)CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS
        ? POWER_MANAGER_STATE_DISCONNECTED_IDLE
        : POWER_MANAGER_STATE_ACTIVE;
}

static bool power_manager_automatic_shutdown_blocked_by_external_power_locked(uint64_t now_ms)
{
    uint32_t hardware_shutdown_ms = power_manager_hardware_shutdown_ms();
    return CONFIG_POWER_MANAGER_ENABLE &&
           power_manager_without_external_power_blocker(s_blockers) == 0 &&
           s_external_power_present &&
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
        user_idle_ms >= hardware_shutdown_ms) {
        return s_external_power_present
            ? power_manager_awake_idle_state_locked(radio_idle_ms)
            : POWER_MANAGER_STATE_HARDWARE_SHUTDOWN;
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
        if (ble_hid_gap_set_low_power_advertising != NULL) {
            (void)ble_hid_gap_set_low_power_advertising(true);
        }
        break;
    default:
        break;
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
    uint64_t now_ms = power_manager_now_ms();
    power_manager_power_source_snapshot_t power_source = {0};
    power_manager_read_power_source(&power_source);

    snapshot->usb_det_level = power_source.usb_det_level;
    snapshot->bat_chg_level = power_source.bat_chg_level;
    snapshot->bat_std_level = power_source.bat_std_level;
    snapshot->pwr_hold_level = power_source.pwr_hold_level;
    snapshot->usb_power_present = power_source.usb_power_present;
    snapshot->external_power_present = power_source.external_power_present;
    snapshot->charging = power_source.charging;
    snapshot->charge_full = power_source.charge_full;
    snapshot->usb_det_policy = power_source.usb_det_policy;
    snapshot->charger_polarity_policy = power_source.charger_polarity_policy;
    snapshot->pwr_hold_policy = power_source.pwr_hold_policy;

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
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
        snapshot->automatic_shutdown_blocked_by_external_power =
            CONFIG_POWER_MANAGER_ENABLE &&
            power_manager_without_external_power_blocker(s_blockers) == 0 &&
            power_source.external_power_present &&
            snapshot->user_idle_ms >= power_manager_hardware_shutdown_ms();
        snapshot->last_shutdown_reason = s_last_shutdown_reason;
        snapshot->last_shutdown_idle_ms = s_last_shutdown_idle_ms;
        snapshot->last_shutdown_blockers = s_last_shutdown_blockers;
        xSemaphoreGive(s_mutex);
    }

    snapshot->connected_idle_threshold_ms = CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS;
    snapshot->audio_idle_threshold_ms = CONFIG_POWER_MANAGER_AUDIO_IDLE_MS;
    snapshot->disconnected_idle_threshold_ms = CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS;
    snapshot->hardware_shutdown_threshold_ms = power_manager_hardware_shutdown_ms();
    snapshot->hardware_shutdown_guard_enabled = CONFIG_POWER_MANAGER_ENABLE != 0;
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    snapshot->pwr_hold_gpio = power_hold.gpio;
    snapshot->pwr_hold_level = power_hold.level;
    snapshot->pwr_hold_configured = power_hold.configured;
    snapshot->pwr_hold_policy = power_hold.policy;
    snapshot->voice_key_gpio = (uint32_t)BOARD_PINS_EC11_KEY_IO;
    snapshot->hardware_shutdown_user_action = POWER_MANAGER_SHUTDOWN_USER_ACTION;
    power_manager_update_battery_snapshot(snapshot);
}

static void power_manager_reset_idle_after_shutdown_failure(void)
{
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        uint64_t reset_ms = power_manager_now_ms();
        s_last_user_activity_ms = reset_ms;
        s_last_radio_activity_ms = reset_ms;
        s_auto_shutdown_block_logged = false;
        s_state = s_ble_connected
            ? POWER_MANAGER_STATE_CONNECTED_IDLE
            : POWER_MANAGER_STATE_DISCONNECTED_IDLE;
        xSemaphoreGive(s_mutex);
    }
}

static void power_manager_wait_for_power_removal(void)
{
    ESP_LOGE(
        TAG,
        "automatic hardware shutdown did not remove power; keeping PWR_HOLD/GPIO11 driven high and staying quiescent");
    if (ble_hid_gap_prepare_shutdown_disconnect != NULL) {
        (void)ble_hid_gap_prepare_shutdown_disconnect();
    }
    (void)board_set_power_hold_enabled(false);

    while (1) {
        watchdog_platform_feed_current_task();
        vTaskDelay(pdMS_TO_TICKS(1000));
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
            .bat_chg_level = snapshot.bat_chg_level,
            .bat_std_level = snapshot.bat_std_level,
            .usb_power_present = snapshot.usb_power_present,
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
    power_manager_read_power_source(&final_power_source);

    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "hardware shutdown rejected: failed to acquire final shutdown gate");
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_WARN,
                 0, snapshot.idle_ms, (uint32_t)reason, (uint32_t)ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

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
        ESP_LOGE(TAG, "hardware shutdown failed: PWR_HOLD/GPIO11 drive-high ret=%s",
                 esp_err_to_name(hold_ret));
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_ERROR,
                 0, final_idle_ms, (uint32_t)reason, (uint32_t)hold_ret);
        power_manager_reset_idle_after_shutdown_failure();
        return hold_ret;
    }

    vTaskDelay(pdMS_TO_TICKS(750));
    if (reason == POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE) {
        diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_ERROR,
                 0, final_idle_ms, (uint32_t)reason, (uint32_t)ESP_FAIL);
        power_manager_wait_for_power_removal();
    }

    ESP_LOGE(
        TAG,
        "hardware shutdown did not remove power after PWR_HOLD/GPIO11 drive-high; restoring runtime low");
    (void)board_set_power_hold_enabled(true);
    if (ble_hid_gap_request_reconnect != NULL) {
        (void)ble_hid_gap_request_reconnect();
    }
    diag_log(DIAG_SRC_POWER, DIAG_POWER_SLEEP_BLOCKED, DIAG_SEV_ERROR,
             0, final_idle_ms, (uint32_t)reason, (uint32_t)ESP_FAIL);
    power_manager_reset_idle_after_shutdown_failure();
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
    power_manager_read_power_source(&power_source);

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    previous = s_state;
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
       USB/VBUS, charging, or charge-full status blocks automatic low-battery
       shutdown even if the battery estimate is critical. */
    power_manager_update_battery_snapshot(&battery_snapshot);
    if (battery_snapshot.battery_valid &&
        battery_snapshot.battery_level_percent <= POWER_MANAGER_BATTERY_CRITICAL_PERCENT &&
        power_manager_low_battery_shutdown_allowed(&power_source)) {
        ESP_LOGW(
            TAG,
            "low battery critical shutdown: level=%u%% mv=%" PRIu32 " threshold=%u%%"
            " usb_power=%u external_power=%u charging=%u charge_full=%u",
            battery_snapshot.battery_level_percent,
            battery_snapshot.battery_mv,
            (unsigned)POWER_MANAGER_BATTERY_CRITICAL_PERCENT,
            power_source.usb_power_present ? 1u : 0u,
            power_source.external_power_present ? 1u : 0u,
            power_source.charging ? 1u : 0u,
            power_source.charge_full ? 1u : 0u);
        esp_err_t lb_ret =
            power_manager_enter_hardware_shutdown(POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY);
        if (lb_ret != ESP_OK) {
            ESP_LOGW(TAG, "low battery shutdown rejected, will retry next evaluate cycle");
        }
        return;
    }

    if (power_source_changed) {
        ESP_LOGI(
            TAG,
            "power source changed: usb_det=%s bat_chg=%s bat_std=%s"
            " pwr_hold=%s usb_power_present=%u charging=%u charge_full=%u external_power_present=%u"
            " usb_policy=%s charger_policy=%s pwr_hold_policy=%s",
            power_manager_gpio_level_name(power_source.usb_det_level),
            power_manager_gpio_level_name(power_source.bat_chg_level),
            power_manager_gpio_level_name(power_source.bat_std_level),
            power_manager_gpio_level_name(power_source.pwr_hold_level),
            power_source.usb_power_present ? 1u : 0u,
            power_source.charging ? 1u : 0u,
            power_source.charge_full ? 1u : 0u,
            power_source.external_power_present ? 1u : 0u,
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
            power_manager_reset_idle_after_shutdown_failure();
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
    if (hold_ret != ESP_OK) {
        ESP_LOGW(TAG, "PWR_HOLD/GPIO11 runtime-low setup failed: %s", esp_err_to_name(hold_ret));
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
    ESP_LOGI(
        TAG,
        "power manager started: audio_idle_ms=%u connected_idle_ms=%u disconnected_idle_ms=%u"
        " hardware_shutdown_ms=%u eval_ms=%u pwr_hold_gpio=%d",
        (unsigned)CONFIG_POWER_MANAGER_AUDIO_IDLE_MS,
        (unsigned)CONFIG_POWER_MANAGER_CONNECTED_IDLE_MS,
        (unsigned)CONFIG_POWER_MANAGER_DISCONNECTED_IDLE_MS,
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
        " ble_connected=%u automatic_shutdown_blocked_by_external_power=%u"
        " external_power_present=%u usb_power_present=%u charging=%u charge_full=%u"
        " usb_det_level=%s bat_chg_level=%s bat_std_level=%s pwr_hold_level=%s"
        " usb_det_policy=%s charger_polarity=%s pwr_hold_policy=%s"
        " battery_mv=%" PRIu32 " battery_level=%u battery_valid=%u"
        " last_shutdown_reason=%s last_shutdown_idle_ms=%" PRIu32
        " last_shutdown_blockers=0x%08" PRIx32 " guard=%u audio_idle_ms=%" PRIu32
        " audio_idle_power_save=%u audio_idle_blockers=0x%08" PRIx32
        " connected_idle_ms=%" PRIu32 " disconnected_idle_ms=%" PRIu32
        " hardware_shutdown_ms=%" PRIu32
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
        snapshot.automatic_shutdown_blocked_by_external_power ? 1u : 0u,
        snapshot.external_power_present ? 1u : 0u,
        snapshot.usb_power_present ? 1u : 0u,
        snapshot.charging ? 1u : 0u,
        snapshot.charge_full ? 1u : 0u,
        power_manager_gpio_level_name(snapshot.usb_det_level),
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
        snapshot.hardware_shutdown_guard_enabled ? 1u : 0u,
        snapshot.audio_idle_threshold_ms,
        snapshot.audio_idle_power_save_enabled ? 1u : 0u,
        snapshot.audio_idle_blockers,
        snapshot.connected_idle_threshold_ms,
        snapshot.disconnected_idle_threshold_ms,
        snapshot.hardware_shutdown_threshold_ms,
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

    power_manager_record_activity("usb_power_command");

    if (strcmp(command, "SHUTDOWN") == 0) {
        (void)power_manager_enter_hardware_shutdown(POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND);
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
