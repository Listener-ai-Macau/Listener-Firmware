#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_LED_BLE_DISCONNECTED = 0,
    STATUS_LED_BLE_PAIRING,
    STATUS_LED_BLE_RECONNECTING,
    STATUS_LED_BLE_CONNECTED,
    STATUS_LED_BLE_TYPE_READY,
    STATUS_LED_BLE_REPAIRING,
} status_led_ble_state_t;

typedef enum {
    STATUS_LED_REC_SOURCE_NONE = 0,
    STATUS_LED_REC_SOURCE_DEVICE_MIC,
    STATUS_LED_REC_SOURCE_DESKTOP_MIC,
    STATUS_LED_REC_SOURCE_NOT_AVAILABLE,
} status_led_rec_source_t;

typedef enum {
    STATUS_LED_ERROR_DOMAIN_NONE = 0,
    STATUS_LED_ERROR_DOMAIN_BLE,
    STATUS_LED_ERROR_DOMAIN_REC,
    STATUS_LED_ERROR_DOMAIN_AI,
    STATUS_LED_ERROR_DOMAIN_OTA,
    STATUS_LED_ERROR_DOMAIN_POWER,
    STATUS_LED_ERROR_DOMAIN_SYSTEM,
} status_led_error_domain_t;

typedef enum {
    STATUS_LED_ERROR_RETRYABLE = 0,
    STATUS_LED_ERROR_HARD,
} status_led_error_severity_t;

typedef enum {
    STATUS_LED_EC11_FEEDBACK_PRESS = 0,
    STATUS_LED_EC11_FEEDBACK_ROTATE_CW,
    STATUS_LED_EC11_FEEDBACK_ROTATE_CCW,
} status_led_ec11_feedback_t;

typedef enum {
    STATUS_LED_KEY_FEEDBACK_SINGLE = 0,
    STATUS_LED_KEY_FEEDBACK_DOUBLE,
    STATUS_LED_KEY_FEEDBACK_LONG,
} status_led_key_feedback_t;

esp_err_t status_led_init(void);
esp_err_t status_led_start(void);
void status_led_show_status_window(const char *reason);
void status_led_log_boot_feedback_after_diag_init(void);
void status_led_set_ble_state(status_led_ble_state_t state, bool confidence_window);
void status_led_set_type_ota_link_active(bool active, const char *reason);
void status_led_note_ble_boot_ready(const char *reason);
void status_led_notify_ble_repairing(const char *reason);
void status_led_notify_ble_repairing_for_ms(const char *reason, uint32_t hold_ms);
void status_led_set_recording(bool active, status_led_rec_source_t source);
void status_led_set_recording_level(uint8_t level_percent);
void status_led_set_processing(bool active, const char *reason);
void status_led_set_ota_active(bool active, size_t bytes_written, size_t expected_size, const char *reason);
void status_led_notify_success(const char *reason);
void status_led_notify_warning(const char *reason);
void status_led_notify_key_event(uint8_t key_index, bool pressed);
void status_led_cancel_key_preview(uint8_t key_index);
void status_led_notify_key_feedback(uint8_t key_index, status_led_key_feedback_t feedback);
void status_led_notify_ec11_feedback(status_led_ec11_feedback_t feedback);
void status_led_refresh_ec11_feedback(status_led_ec11_feedback_t feedback);
void status_led_notify_shutdown_confirm(bool final, const char *reason);
bool status_led_try_notify_shutdown_confirm(bool final, const char *reason, uint32_t wait_ms);
bool status_led_try_notify_shutdown_final_hold(const char *reason, uint32_t wait_ms);
bool status_led_try_hold_shutdown_all_off(const char *reason, uint32_t wait_ms);
void status_led_cancel_shutdown_confirm(const char *reason);
void status_led_set_error(
    status_led_error_domain_t domain,
    status_led_error_severity_t severity,
    const char *reason);
void status_led_clear_error(status_led_error_domain_t domain);
void status_led_set_low_power_disabled(bool disabled);
void status_led_prepare_sleep(void);
void status_led_apply_device_settings(void);
bool status_led_consume_usb_command(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_H */
