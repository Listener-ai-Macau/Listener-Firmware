#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_MANAGER_STATE_ACTIVE = 0,
    POWER_MANAGER_STATE_CONNECTED_IDLE,
    POWER_MANAGER_STATE_DISCONNECTED_IDLE,
    POWER_MANAGER_STATE_HARDWARE_SHUTDOWN,
} power_manager_state_t;

typedef enum {
    POWER_MANAGER_BLOCKER_RECORDING = 1u << 0,
    POWER_MANAGER_BLOCKER_BLE_AUDIO = 1u << 1,
    POWER_MANAGER_BLOCKER_DIAG_EXPORT = 1u << 2,
    POWER_MANAGER_BLOCKER_PAIRING = 1u << 3,
    POWER_MANAGER_BLOCKER_RECONNECT = 1u << 4,
    POWER_MANAGER_BLOCKER_FLASH_WRITE = 1u << 5,
    POWER_MANAGER_BLOCKER_USB_COMMAND = 1u << 6,
    POWER_MANAGER_BLOCKER_EXTERNAL_POWER = 1u << 7,
    POWER_MANAGER_BLOCKER_OTA = 1u << 8,
    POWER_MANAGER_BLOCKER_VOICE_ACTIVATION = 1u << 9,
} power_manager_blocker_t;

typedef enum {
    POWER_MANAGER_SHUTDOWN_REASON_NONE = 0,
    POWER_MANAGER_SHUTDOWN_REASON_LONG_IDLE = 1,
    POWER_MANAGER_SHUTDOWN_REASON_MANUAL_COMMAND = 2,
    POWER_MANAGER_SHUTDOWN_REASON_LOW_BATTERY = 3,
} power_manager_shutdown_reason_t;

typedef struct {
    power_manager_state_t state;
    uint32_t blockers;
    uint32_t shutdown_blockers;
    uint32_t idle_ms;
    uint32_t user_idle_ms;
    uint32_t radio_idle_ms;
    uint32_t audio_idle_threshold_ms;
    uint32_t audio_idle_blockers;
    uint32_t low_power_idle_threshold_ms;
    uint32_t plugged_low_power_idle_threshold_ms;
    uint32_t battery_low_power_idle_threshold_ms;
    uint32_t connected_idle_threshold_ms;
    uint32_t disconnected_idle_threshold_ms;
    uint32_t hardware_shutdown_threshold_ms;
    uint32_t plugged_auto_shutdown_threshold_ms;
    uint32_t battery_auto_shutdown_threshold_ms;
    uint32_t battery_mv;
    uint8_t battery_level_percent;
    bool battery_valid;
    bool ble_connected;
    bool audio_idle_power_save_enabled;
    bool hardware_shutdown_guard_enabled;
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
    bool plugged_low_power_enabled;
    bool low_power_idle_allowed;
    bool power_input_wake_configured;
    bool power_input_irq_armed;
    bool charge_full_latched;
    uint32_t charge_full_candidate_ms;
    uint32_t charge_full_debounce_ms;
    uint32_t charge_full_min_mv;
    uint8_t charge_full_min_percent;
    bool automatic_shutdown_blocked_by_external_power;
    const char *usb_det_policy;
    const char *charger_polarity_policy;
    const char *pwr_hold_policy;
    power_manager_shutdown_reason_t last_shutdown_reason;
    uint32_t last_shutdown_idle_ms;
    uint32_t last_shutdown_blockers;
    esp_err_t last_shutdown_failure_ret;
    uint32_t shutdown_failure_retry_ms_left;
    uint32_t shutdown_failure_retry_ms;
    bool last_shutdown_persisted;
    uint32_t last_shutdown_battery_mv;
    uint8_t last_shutdown_battery_level_percent;
    uint32_t last_shutdown_power_flags;
    int pwr_hold_gpio;
    bool pwr_hold_configured;
    uint32_t voice_key_gpio;
    const char *hardware_shutdown_user_action;
} power_manager_snapshot_t;

esp_err_t power_manager_init(void);
esp_err_t power_manager_start(void);
void power_manager_record_activity(const char *reason);
void power_manager_set_blocker(uint32_t blocker_mask, bool enabled);
/* Atomically reserve the active power state for a hidden voice candidate.
 * Unlike set_blocker(), this refuses to resurrect an already-idle device. */
bool power_manager_try_acquire_voice_activation(void);
void power_manager_set_ble_connected(bool connected);
bool power_manager_consume_usb_command(const char *line);
power_manager_state_t power_manager_get_state(void);
void power_manager_get_snapshot(power_manager_snapshot_t *snapshot);

const char *power_manager_state_name(power_manager_state_t state);
const char *power_manager_shutdown_reason_name(power_manager_shutdown_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */
