#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_SETTINGS_DEFAULT_BLE_NAME "listener"
#define DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT 80U
#define DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT 50U
#define DEVICE_SETTINGS_BLE_NAME_MAX_LEN 32

typedef struct {
    uint8_t plugged_brightness_percent;
    uint8_t battery_brightness_percent;
    uint32_t battery_auto_shutdown_ms;
    char ble_name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1];
    bool ble_name_pending_restart;
    bool loaded_from_nvs;
} device_settings_snapshot_t;

esp_err_t device_settings_init(void);
void device_settings_get_snapshot(device_settings_snapshot_t *out_snapshot);
uint8_t device_settings_get_active_brightness_percent(bool external_power_present);
uint32_t device_settings_get_battery_auto_shutdown_ms(void);
const char *device_settings_get_ble_name(void);
bool device_settings_ble_name_pending_restart(void);
esp_err_t device_settings_set_brightness_profiles(uint8_t plugged_percent, uint8_t battery_percent);
esp_err_t device_settings_consume_control_command(const char *line);
bool device_settings_consume_usb_command(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_SETTINGS_H */
