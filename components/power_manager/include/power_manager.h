#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_MANAGER_STATE_ACTIVE = 0,
    POWER_MANAGER_STATE_CONNECTED_IDLE,
    POWER_MANAGER_STATE_DISCONNECTED_IDLE,
    POWER_MANAGER_STATE_OVERNIGHT_SLEEP,
} power_manager_state_t;

typedef enum {
    POWER_MANAGER_BLOCKER_RECORDING = 1u << 0,
    POWER_MANAGER_BLOCKER_BLE_AUDIO = 1u << 1,
    POWER_MANAGER_BLOCKER_DIAG_EXPORT = 1u << 2,
    POWER_MANAGER_BLOCKER_PAIRING = 1u << 3,
    POWER_MANAGER_BLOCKER_RECONNECT = 1u << 4,
    POWER_MANAGER_BLOCKER_FLASH_WRITE = 1u << 5,
    POWER_MANAGER_BLOCKER_USB_COMMAND = 1u << 6,
} power_manager_blocker_t;

typedef enum {
    POWER_MANAGER_SLEEP_REASON_NONE = 0,
    POWER_MANAGER_SLEEP_REASON_OVERNIGHT_IDLE = 1,
    POWER_MANAGER_SLEEP_REASON_MANUAL_COMMAND = 2,
} power_manager_sleep_reason_t;

typedef enum {
    POWER_MANAGER_WAKE_SOURCE_UNDEFINED = 0,
    POWER_MANAGER_WAKE_SOURCE_EXT0,
    POWER_MANAGER_WAKE_SOURCE_EXT1,
    POWER_MANAGER_WAKE_SOURCE_TIMER,
    POWER_MANAGER_WAKE_SOURCE_TOUCHPAD,
    POWER_MANAGER_WAKE_SOURCE_ULP,
    POWER_MANAGER_WAKE_SOURCE_GPIO,
    POWER_MANAGER_WAKE_SOURCE_UART,
    POWER_MANAGER_WAKE_SOURCE_POWER_ON,
} power_manager_wake_source_t;

typedef enum {
    POWER_MANAGER_WAKE_POLICY_KEY4_ONLY = 1,
    POWER_MANAGER_WAKE_POLICY_V2_EC11_PROVISIONAL = 2,
} power_manager_wake_policy_t;

typedef struct {
    power_manager_state_t state;
    uint32_t blockers;
    uint32_t idle_ms;
    uint32_t user_idle_ms;
    uint32_t radio_idle_ms;
    uint32_t audio_idle_threshold_ms;
    uint32_t connected_idle_threshold_ms;
    uint32_t disconnected_idle_threshold_ms;
    uint32_t overnight_sleep_threshold_ms;
    uint32_t battery_mv;
    uint8_t battery_level_percent;
    bool battery_valid;
    bool ble_connected;
    bool overnight_guard_enabled;
    power_manager_sleep_reason_t last_sleep_reason;
    power_manager_wake_source_t last_wake_source;
    bool last_sleep_stats_valid;
    uint32_t last_sleep_duration_ms;
    uint32_t sleep_entry_battery_mv;
    uint8_t sleep_entry_battery_level_percent;
    bool sleep_entry_battery_valid;
    uint32_t wake_battery_mv;
    uint8_t wake_battery_level_percent;
    bool wake_battery_valid;
    int32_t sleep_drain_mv;
    int32_t sleep_drain_level_percent;
    int32_t sleep_drain_mv_per_hour;
    int32_t sleep_drain_level_per_hour_x100;
    uint64_t wake_gpio_mask;
    power_manager_wake_policy_t wake_policy;
    uint32_t wake_key_gpio;
    bool wake_key_rtc_capable;
    uint32_t voice_key_gpio;
    bool voice_key_rtc_capable;
    bool voice_key_deep_sleep_wake_enabled;
    const char *wake_capable_keys;
    const char *voice_key_limitation;
    const char *wake_user_action;
} power_manager_snapshot_t;

esp_err_t power_manager_init(void);
esp_err_t power_manager_start(void);
void power_manager_record_activity(const char *reason);
void power_manager_set_blocker(uint32_t blocker_mask, bool enabled);
void power_manager_set_ble_connected(bool connected);
bool power_manager_consume_usb_command(const char *line);
void power_manager_get_snapshot(power_manager_snapshot_t *snapshot);

const char *power_manager_state_name(power_manager_state_t state);
const char *power_manager_sleep_reason_name(power_manager_sleep_reason_t reason);
const char *power_manager_wake_source_name(power_manager_wake_source_t source);
power_manager_wake_source_t power_manager_map_wakeup(esp_sleep_wakeup_cause_t cause);
const char *power_manager_wake_policy_name(power_manager_wake_policy_t policy);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */
