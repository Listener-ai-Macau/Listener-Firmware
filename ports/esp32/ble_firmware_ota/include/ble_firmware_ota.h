#ifndef BLE_FIRMWARE_OTA_H
#define BLE_FIRMWARE_OTA_H

#include <stdint.h>

#include "denzic_ota_v1_generated.h"
#include "esp_err.h"
#include "nimble/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_FIRMWARE_OTA_SERVICE_UUID \
    BLE_UUID128_INIT(DENZIC_OTA_V1_GATT_SERVICE_UUID_BYTES)
#define BLE_FIRMWARE_OTA_READINESS_UUID \
    BLE_UUID128_INIT(0x1c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_FIRMWARE_OTA_CAPABILITIES_UUID \
    BLE_UUID128_INIT(0x1d, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_FIRMWARE_OTA_V1_CONTROL_UUID \
    BLE_UUID128_INIT(DENZIC_OTA_V1_GATT_CONTROL_UUID_BYTES)
#define BLE_FIRMWARE_OTA_V1_DATA_UUID \
    BLE_UUID128_INIT(DENZIC_OTA_V1_GATT_DATA_UUID_BYTES)
/* Optional second data lane (same access handler) so the host can keep two
 * WWR pipelines filled on Windows — companion dual-lane pattern for OTA. */
#define BLE_FIRMWARE_OTA_V1_DATA_B_UUID \
    BLE_UUID128_INIT(0x13, 0x7e, 0x9a, 0x0a, 0xe9, 0xe5, 0xe4, 0xab, 0xd1, 0x4b, 0x02, 0x61, 0xfb, 0xb0, 0xc4, 0xfb)
#define BLE_FIRMWARE_OTA_V1_STATUS_UUID \
    BLE_UUID128_INIT(DENZIC_OTA_V1_GATT_STATUS_UUID_BYTES)

esp_err_t ble_firmware_ota_register_gatt(void);
void ble_firmware_ota_on_gap_disconnect(uint16_t conn_handle);
void ble_firmware_ota_log_gatt_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_FIRMWARE_OTA_H */
