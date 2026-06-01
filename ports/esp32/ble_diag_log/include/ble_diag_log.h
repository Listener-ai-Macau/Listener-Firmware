#ifndef BLE_DIAG_LOG_H
#define BLE_DIAG_LOG_H

#include <stdint.h>

#include "nimble/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE Diagnostic Log Service — UUID allocation.
 * Base: 09c3 3b5b-9e4d-0c83-659f-6d45-f80a-71XX
 * Audio stream uses 0x1a..0x1d, OTA uses 0x2a..0x2c + shared 0x1c/0x1d.
 * Diagnostic log uses 0x3a..0x3d.
 */

#define BLE_DIAG_LOG_SERVICE_UUID \
    BLE_UUID128_INIT(0x3a, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, \
                     0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)

#define BLE_DIAG_LOG_CONTROL_UUID \
    BLE_UUID128_INIT(0x3b, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, \
                     0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)

#define BLE_DIAG_LOG_DATA_UUID \
    BLE_UUID128_INIT(0x3c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, \
                     0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)

#define BLE_DIAG_LOG_COUNT_UUID \
    BLE_UUID128_INIT(0x3d, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, \
                     0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)

/*
 * Register the BLE Diagnostic Log GATT service.
 * Must be called from ble_hid_init() before esp_hidd_dev_init().
 */
int ble_diag_log_register_gatt(void);

/*
 * Track the active GAP connection so diagnostic export can safely size
 * notifications even when MTU exchange completes before the first control
 * write.
 */
void ble_diag_log_on_gap_connect(uint16_t conn_handle);

/*
 * Reset any in-progress paginated read state on BLE disconnect.
 */
void ble_diag_log_on_gap_disconnect(uint16_t conn_handle);

/*
 * Track MTU changes for chunk sizing.
 */
void ble_diag_log_on_gap_mtu(uint16_t conn_handle, uint16_t mtu);

/*
 * Log current GATT handle state for debugging.
 */
void ble_diag_log_log_gatt_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_DIAG_LOG_H */
