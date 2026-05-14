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

typedef struct {
    uint32_t session_id;
    uint32_t duration_seconds;
    uint32_t frame_count;
    const uint8_t *pcm_buffer;
    size_t pcm_bytes;
    uint16_t frame_bytes;
    uint16_t chunk_pcm_bytes;
} ble_audio_stream_export_t;

esp_err_t ble_audio_stream_init(void);
esp_err_t ble_audio_stream_start(void);
void ble_audio_stream_on_gap_connect(uint16_t conn_handle);
void ble_audio_stream_on_gap_disconnect(uint16_t conn_handle);
void ble_audio_stream_on_gap_subscribe(
    uint16_t conn_handle,
    uint16_t attr_handle,
    uint8_t cur_notify,
    uint8_t cur_indicate);
void ble_audio_stream_on_gap_mtu(uint16_t conn_handle, uint16_t mtu);
esp_err_t ble_audio_stream_register_gatt(void);
esp_err_t ble_audio_stream_send_session_start(uint32_t session_id);
esp_err_t ble_audio_stream_send_session_chunk(
    uint32_t session_id,
    uint16_t chunk_index,
    const uint8_t *pcm_buffer,
    uint16_t pcm_bytes);
esp_err_t ble_audio_stream_send_session_stop(uint32_t session_id, uint16_t chunk_count);
esp_err_t ble_audio_stream_send_session_cancel(uint32_t session_id, uint16_t chunk_count);
esp_err_t ble_audio_stream_send_export(const ble_audio_stream_export_t *export_info);
esp_err_t ble_audio_stream_send_export_blocking(const ble_audio_stream_export_t *export_info);
bool ble_audio_stream_is_ready(void);
uint16_t ble_audio_stream_get_notify_attr_handle(void);
void ble_audio_stream_log_gatt_state(void);

#ifdef __cplusplus
}
#endif

#endif
