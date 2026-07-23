#ifndef BLE_DIAG_LOG_H
#define BLE_DIAG_LOG_H

#include <stdint.h>

#include "nimble/ble.h"

#include "denzic_observability_v1_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE Diagnostic Log Service — UUIDs come from the shared platform contract
 * (ble_diag_log_gatt in observability/protocol/observability_v1.json).
 * Base: 09c3 3b5b-9e4d-0c83-659f-6d45-f80a-71XX; diagnostic log uses 0x3a..0x3d.
 */

#define BLE_DIAG_LOG_SERVICE_UUID \
    BLE_UUID128_INIT(DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_SERVICE_UUID_BYTES)

#define BLE_DIAG_LOG_CONTROL_UUID \
    BLE_UUID128_INIT(DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_CONTROL_UUID_BYTES)

#define BLE_DIAG_LOG_DATA_UUID \
    BLE_UUID128_INIT(DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_DATA_UUID_BYTES)

#define BLE_DIAG_LOG_COUNT_UUID \
    BLE_UUID128_INIT(DENZIC_OBSERVABILITY_V1_DIAG_LOG_GATT_COUNT_UUID_BYTES)

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
