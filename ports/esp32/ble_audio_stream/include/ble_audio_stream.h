#ifndef BLE_AUDIO_STREAM_H
#define BLE_AUDIO_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "nimble/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_AUDIO_STREAM_SERVICE_UUID \
    BLE_UUID128_INIT(0x1a, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_AUDIO_STREAM_NOTIFY_UUID \
    BLE_UUID128_INIT(0x1b, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_AUDIO_STREAM_CONTROL_UUID \
    BLE_UUID128_INIT(0x1e, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_AUDIO_STREAM_READINESS_UUID \
    BLE_UUID128_INIT(0x1c, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)
#define BLE_AUDIO_STREAM_CAPABILITIES_UUID \
    BLE_UUID128_INIT(0x1d, 0x09, 0xc3, 0x3b, 0x5b, 0x9e, 0x4d, 0x0c, 0x83, 0x65, 0x9f, 0x6d, 0x45, 0xf8, 0x0a, 0x71)

typedef struct {
    uint32_t queue_depth;
    uint32_t queue_capacity;
    uint32_t audio_pool_in_use;
    uint32_t audio_pool_capacity;
    uint32_t audio_pool_high_water;
    uint32_t pressure_percent;
    bool transport_session_active;
    bool link_ready;
    bool pause_recommended;
    bool resume_recommended;
} ble_audio_stream_backpressure_t;

typedef esp_err_t (*ble_audio_stream_control_write_handler_t)(
    const uint8_t *data,
    size_t len,
    const char *source);

esp_err_t ble_audio_stream_init(void);
void ble_audio_stream_set_control_write_handler(ble_audio_stream_control_write_handler_t handler);
void ble_audio_stream_on_gap_connect(uint16_t conn_handle);
void ble_audio_stream_on_gap_disconnect(uint16_t conn_handle);
void ble_audio_stream_on_gap_subscribe(
    uint16_t conn_handle,
    uint16_t attr_handle,
    uint8_t cur_notify,
    uint8_t cur_indicate);
void ble_audio_stream_on_gap_mtu(uint16_t conn_handle, uint16_t mtu);
void ble_audio_stream_on_gap_notify_tx(
    uint16_t conn_handle,
    uint16_t attr_handle,
    int status,
    bool indication);
esp_err_t ble_audio_stream_register_gatt(void);
uint16_t ble_audio_stream_get_audio_payload_bytes(void);
uint16_t ble_audio_stream_count_audio_packets(uint16_t pcm_bytes);
esp_err_t ble_audio_stream_send_session_start(uint32_t session_id);
esp_err_t ble_audio_stream_send_session_audio(
    uint32_t session_id,
    uint16_t packet_sequence,
    const uint8_t *pcm_buffer,
    uint16_t pcm_bytes);
esp_err_t ble_audio_stream_send_session_stop(uint32_t session_id, uint16_t expected_packet_count);
esp_err_t ble_audio_stream_send_session_cancel(uint32_t session_id, uint16_t expected_packet_count);
esp_err_t ble_audio_stream_send_session_error(
    uint32_t session_id,
    uint16_t expected_packet_count,
    uint16_t error_code);
esp_err_t ble_audio_stream_send_type_recovery_notice(void);
bool ble_audio_stream_is_ready(void);
bool ble_audio_stream_is_type_link_ready(void);
bool ble_audio_stream_is_type_led_ready(void);
bool ble_audio_stream_was_type_host_recently_seen(void);
bool ble_audio_stream_consume_type_control_command(const char *command, const char *source);
void ble_audio_stream_note_type_activity(const char *reason);
uint32_t ble_audio_stream_type_link_poll_wait_ms(uint32_t fallback_ms);
void ble_audio_stream_poll_type_link(void);
bool ble_audio_stream_is_busy(void);
void ble_audio_stream_get_backpressure(ble_audio_stream_backpressure_t *snapshot);
uint16_t ble_audio_stream_get_notify_attr_handle(void);
void ble_audio_stream_log_gatt_state(void);

#ifdef __cplusplus
}
#endif

#endif
