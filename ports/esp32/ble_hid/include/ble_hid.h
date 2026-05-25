#ifndef BLE_HID_H
#define BLE_HID_H

#ifdef __cplusplus
extern "C" {
#endif

void ble_hid_init(void);
void ble_hid_start(void);
void ble_hid_task_start_up(void);
bool ble_hid_is_connected(void);
uint32_t ble_hid_get_disconnect_count(void);

#ifdef __cplusplus
}
#endif

#endif
