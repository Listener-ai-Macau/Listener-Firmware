#ifndef BLE_HID_H
#define BLE_HID_H

#ifdef __cplusplus
extern "C" {
#endif

void ble_hid_init(void);
void ble_hid_start(void);
void ble_hid_task_start_up(void);

#ifdef __cplusplus
}
#endif

#endif
