#ifndef BLE_HID_GAP_H
#define BLE_HID_GAP_H

#include "esp_err.h"
#include "esp_hid_common.h"

#define HIDD_IDLE_MODE 0x00
#define HIDD_BLE_MODE 0x01
#define HIDD_BT_MODE 0x02
#define HIDD_BTDM_MODE 0x03

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_hid_gap_init(void);
esp_err_t ble_hid_gap_configure_advertising(uint16_t appearance, const char *device_name);
esp_err_t ble_hid_gap_start_advertising(void);

esp_err_t esp_hid_gap_init(uint8_t mode);
esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name);
esp_err_t esp_hid_ble_gap_adv_start(void);

#ifdef __cplusplus
}
#endif

#endif
