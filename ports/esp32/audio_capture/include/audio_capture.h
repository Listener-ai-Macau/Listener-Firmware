#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_capture_start(void);
bool audio_capture_is_running(void);
bool audio_capture_request_fixed_export_seconds(uint32_t duration_seconds);
esp_err_t audio_capture_session_begin(void);
esp_err_t audio_capture_session_stop(void);
esp_err_t audio_capture_session_cancel(void);
bool audio_capture_session_is_active(void);

typedef void (*audio_capture_export_callback_t)(
    uint32_t session_id,
    uint32_t duration_seconds,
    uint32_t frame_count,
    const uint8_t *pcm_buffer,
    size_t pcm_bytes,
    uint16_t frame_bytes);

void audio_capture_set_export_callback(audio_capture_export_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif
