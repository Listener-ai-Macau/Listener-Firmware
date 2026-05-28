#ifndef BLE_HID_GAP_H
#define BLE_HID_GAP_H

#define HIDD_BLE_MODE 0x01

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_hid_gap_init(void);
void ble_hid_gap_set_audio_enabled(bool enabled);
esp_err_t ble_hid_gap_configure_advertising(uint16_t appearance, const char *device_name);
esp_err_t ble_hid_gap_start_advertising(void);
esp_err_t ble_hid_gap_mark_stack_ready(void);
esp_err_t ble_hid_gap_forget_bonds_and_repair(void);
bool ble_hid_gap_is_connected(void);
esp_err_t ble_hid_gap_set_low_power_advertising(bool enabled);
esp_err_t ble_hid_gap_request_low_power_connection(void);
esp_err_t ble_hid_gap_request_active_connection(void);

#ifdef __cplusplus
}
#endif

#endif
