#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ble_hid_usb_command_handler_t)(const char *line, esp_err_t *out_ret);

esp_err_t ble_hid_init(void);
esp_err_t ble_hid_start(void);
void ble_hid_task_start_up(void);
void ble_hid_set_safe_mode(bool enabled);
bool ble_hid_is_connected(void);
uint32_t ble_hid_get_disconnect_count(void);
void ble_hid_register_usb_command_handler(ble_hid_usb_command_handler_t handler);
esp_err_t ble_hid_send_ascii_async(char input_char);
esp_err_t ble_hid_send_keyboard_usage_async(uint8_t usage, const char *source);
esp_err_t ble_hid_send_keyboard_usage_with_modifier_async(uint8_t usage, uint8_t modifier, const char *source);
esp_err_t ble_hid_send_consumer_usage_async(uint16_t usage, const char *source);

#ifdef __cplusplus
}
#endif

#endif
