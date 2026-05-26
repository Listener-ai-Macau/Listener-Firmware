#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_hid_init(void);
esp_err_t ble_hid_start(void);
void ble_hid_task_start_up(void);
bool ble_hid_is_connected(void);
uint32_t ble_hid_get_disconnect_count(void);
esp_err_t ble_hid_send_ascii_async(char input_char);

#ifdef __cplusplus
}
#endif

#endif
