#ifndef BLE_HID_GAP_H
#define BLE_HID_GAP_H

#define HIDD_BLE_MODE 0x01

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_hid_gap_init(void);
void ble_hid_gap_set_audio_enabled(bool enabled);
esp_err_t ble_hid_gap_configure_advertising(uint16_t appearance, const char *device_name);
esp_err_t ble_hid_gap_start_advertising(void);
esp_err_t ble_hid_gap_mark_stack_ready(void);
esp_err_t ble_hid_gap_forget_bonds_and_repair(void);
/* EC11 idle recovery issues the GAP terminate before non-critical diagnostics. */
esp_err_t ble_hid_gap_forget_bonds_and_repair_ec11_fast(void);
esp_err_t ble_hid_gap_forget_bonds_and_repair_type_controlled(void);
esp_err_t ble_hid_gap_forget_bonds_and_repair_type_controlled_silent(void);
void ble_hid_gap_note_ec11_recovery_accepted(uint64_t accepted_at_us, bool generated);
bool ble_hid_gap_is_connected(void);
bool ble_hid_gap_is_securely_connected(void);
bool ble_hid_gap_is_recovery_pairing_window_open(void);
bool ble_hid_gap_note_type_audio_ready(const char *reason);
esp_err_t ble_hid_gap_set_low_power_advertising(bool enabled);
esp_err_t ble_hid_gap_stop_advertising_for_key_wake(void);
esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void);
void ble_hid_gap_set_ec11_fast_recording_enabled(bool enabled, const char *reason);
esp_err_t ble_hid_gap_request_low_power_connection(void);
esp_err_t ble_hid_gap_request_active_connection(void);
esp_err_t ble_hid_gap_schedule_active_connection(void);
bool ble_hid_gap_active_connection_applied(void);
/* Returns the largest single-link-layer ATT value after DLE, or zero until
 * the active connection has reported its negotiated data length. */
uint16_t ble_hid_gap_get_audio_notification_value_max_bytes(void);
void ble_hid_gap_print_status(void);
bool ble_hid_gap_ota_connection_ready(void);
esp_err_t ble_hid_gap_schedule_ota_reconnect(void);
esp_err_t ble_hid_gap_request_reconnect(void);
esp_err_t ble_hid_gap_apply_pending_ble_name(void);

#ifdef __cplusplus
}
#endif

#endif
