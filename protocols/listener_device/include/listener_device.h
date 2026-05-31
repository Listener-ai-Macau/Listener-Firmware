#ifndef LISTENER_DEVICE_H
#define LISTENER_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LISTENER_DEVICE_MANUFACTURER "listener"
#define LISTENER_DEVICE_BLE_NAME     "listener"
#define LISTENER_DEVICE_MODEL        "keyboard-v2"
#define LISTENER_DEVICE_HW_REV       "esp32s3-wroom-1-n4"

#define LISTENER_VENDOR_ID  0x16C0
#define LISTENER_PRODUCT_ID 0x05DF

#define LISTENER_PROTOCOL_VERSION 1
#define LISTENER_PROTOCOL_VERSION_STR "1"
#define LISTENER_DEVICE_BLE_APPEARANCE_KEYBOARD 0x03C1

#define LISTENER_DEVICE_FACTORY_READINESS \
    "factory_ready;pairable_on_boot;post_degraded_boot;board=voice-keyboard-n4"
#define LISTENER_DEVICE_CAPABILITIES \
    "ble_hid_keyboard;ble_audio_vka1;usb_serial_text;key1_record_toggle;post_status;firmware_ota_v1;flash_4mb;no_psram"

#define LISTENER_DEVICE_READY_HID        (1u << 0)
#define LISTENER_DEVICE_READY_AUDIO      (1u << 1)
#define LISTENER_DEVICE_READY_OTA        (1u << 2)
#define LISTENER_DEVICE_READY_DIAGNOSTIC (1u << 3)

const char *listener_device_get_fw_version(void);
const char *listener_device_get_build_id(void);
const char *listener_device_get_serial(void);
const char *listener_device_get_ble_name(void);
const char *listener_device_get_protocol_version(void);
const char *listener_device_get_factory_readiness(void);
const char *listener_device_get_capabilities(void);
void listener_device_set_safe_mode(bool enabled);
void listener_device_set_readiness(uint32_t ready_mask, uint32_t degraded_mask);
uint32_t listener_device_get_ready_mask(void);
uint32_t listener_device_get_degraded_mask(void);

#ifdef __cplusplus
}
#endif

#endif
