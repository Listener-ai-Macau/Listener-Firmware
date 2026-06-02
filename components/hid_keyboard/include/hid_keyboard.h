#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_hidd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_KEYBOARD_USAGE_F13 0x68u
#define HID_KEYBOARD_USAGE_F14 0x69u
#define HID_KEYBOARD_USAGE_F15 0x6Au
#define HID_KEYBOARD_USAGE_F16 0x6Bu
#define HID_KEYBOARD_USAGE_F17 0x6Cu
#define HID_KEYBOARD_USAGE_F18 0x6Du
#define HID_KEYBOARD_USAGE_F19 0x6Eu
#define HID_KEYBOARD_USAGE_F20 0x6Fu
#define HID_KEYBOARD_USAGE_F21 0x70u
#define HID_KEYBOARD_USAGE_F22 0x71u
#define HID_KEYBOARD_USAGE_F23 0x72u
#define HID_KEYBOARD_USAGE_F24 0x73u

#define HID_CONSUMER_USAGE_BRIGHTNESS_INCREMENT 0x006Fu
#define HID_CONSUMER_USAGE_BRIGHTNESS_DECREMENT 0x0070u
#define HID_CONSUMER_USAGE_VOLUME_INCREMENT     0x00E9u
#define HID_CONSUMER_USAGE_VOLUME_DECREMENT     0x00EAu

void hid_keyboard_init(void);
const uint8_t *hid_keyboard_get_report_map(void);
size_t hid_keyboard_get_report_map_size(void);
uint32_t hid_keyboard_get_key_press_count(void);
esp_err_t hid_keyboard_send_ascii(char input_char, esp_hidd_dev_t *hid_device);
esp_err_t hid_keyboard_send_usage(uint8_t usage, esp_hidd_dev_t *hid_device);
esp_err_t hid_keyboard_send_consumer_usage(uint16_t usage, esp_hidd_dev_t *hid_device);

#ifdef __cplusplus
}
#endif

#endif
