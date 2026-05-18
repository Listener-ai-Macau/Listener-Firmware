#ifndef LISTENER_DEVICE_H
#define LISTENER_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LISTENER_DEVICE_MANUFACTURER "listener"
#define LISTENER_DEVICE_MODEL        "keyboard-v1"
#define LISTENER_DEVICE_HW_REV       "esp32s3-devkit"

#define LISTENER_VENDOR_ID  0x16C0
#define LISTENER_PRODUCT_ID 0x05DF

#define LISTENER_PROTOCOL_VERSION 1

const char *listener_device_get_fw_version(void);
const char *listener_device_get_build_id(void);
const char *listener_device_get_serial(void);

#ifdef __cplusplus
}
#endif

#endif
