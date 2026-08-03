#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_SETTINGS_DEFAULT_BLE_NAME "listener"
#define DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT 100U
#define DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT
#define DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT 50U
#define DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT 80U
#define DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT 100U
#define DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS 60000U
#define DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_IDLE_MS 180000U
#define DEVICE_SETTINGS_DEFAULT_BATTERY_LOW_POWER_IDLE_MS DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS
#define DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED 1
#define DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS 0U /* External power never auto-shuts down. */
#define DEVICE_SETTINGS_DEFAULT_EC11_FAST_RECORDING_ENABLED 1
#define DEVICE_SETTINGS_DEFAULT_VOICE_AUTO_START_ENABLED 1
#define DEVICE_SETTINGS_DEFAULT_VOICE_AUTO_STOP_ENABLED 1
#define DEVICE_SETTINGS_BLE_NAME_MAX_LEN 29

typedef struct {
    uint8_t plugged_brightness_percent;
    uint8_t battery_brightness_percent;
    uint8_t status_led_brightness_percent;
    uint8_t key_led_brightness_percent;
    uint8_t ec11_led_brightness_percent;
    uint8_t edge_led_brightness_percent;
    uint32_t low_power_idle_ms;
    uint32_t plugged_low_power_idle_ms;
    uint32_t battery_low_power_idle_ms;
    bool plugged_low_power_enabled;
    uint32_t plugged_auto_shutdown_ms;
    uint32_t battery_auto_shutdown_ms;
    uint32_t settings_revision;
    bool ec11_fast_recording_enabled;
    bool voice_auto_start_enabled;
    bool voice_auto_stop_enabled;
    char ble_name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1];
    bool ble_name_pending_restart;
    bool loaded_from_nvs;
} device_settings_snapshot_t;

esp_err_t device_settings_init(void);
void device_settings_get_snapshot(device_settings_snapshot_t *out_snapshot);
uint8_t device_settings_get_active_brightness_percent(bool external_power_present);
uint32_t device_settings_get_low_power_idle_ms(void);
uint32_t device_settings_get_active_low_power_idle_ms(bool external_power_present);
uint32_t device_settings_get_plugged_low_power_idle_ms(void);
uint32_t device_settings_get_battery_low_power_idle_ms(void);
bool device_settings_get_plugged_low_power_enabled(void);
uint32_t device_settings_get_active_auto_shutdown_ms(bool external_power_present);
uint32_t device_settings_get_plugged_auto_shutdown_ms(void);
uint32_t device_settings_get_battery_auto_shutdown_ms(void);
uint32_t device_settings_get_revision(void);
bool device_settings_get_ec11_fast_recording_enabled(void);
bool device_settings_get_voice_auto_start_enabled(void);
bool device_settings_get_voice_auto_stop_enabled(void);
const char *device_settings_get_ble_name(void);
bool device_settings_ble_name_pending_restart(void);
void device_settings_mark_ble_name_applied(void);
esp_err_t device_settings_set_brightness_profiles(uint8_t plugged_percent, uint8_t battery_percent);
esp_err_t device_settings_consume_control_command(const char *line);
bool device_settings_consume_usb_command(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_SETTINGS_H */
