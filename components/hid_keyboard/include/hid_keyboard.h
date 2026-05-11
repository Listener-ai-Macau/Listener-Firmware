#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "esp_hidd.h"

#ifdef __cplusplus
extern "C" {
#endif

void hid_keyboard_init(void);
const uint8_t *hid_keyboard_get_report_map(void);
size_t hid_keyboard_get_report_map_size(void);
void hid_keyboard_send_ascii(char input_char, esp_hidd_dev_t *hid_device);

#ifdef __cplusplus
}
#endif

#endif
