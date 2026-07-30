#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_capture_voice_activity_handler_t)(
    bool speech_detected,
    uint32_t elapsed_ms);

typedef enum {
    AUDIO_CAPTURE_STOP_ORIGIN_USER = 0,
    AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION = 1,
} audio_capture_stop_origin_t;

esp_err_t audio_capture_start(void);
bool audio_capture_is_available(void);
const char *audio_capture_get_unavailable_reason(void);
esp_err_t audio_capture_set_idle_power_save(bool enabled);
bool audio_capture_idle_power_save_is_applied(void);
esp_err_t audio_capture_set_ota_suspended(bool suspended);
esp_err_t audio_capture_session_begin(void);
esp_err_t audio_capture_session_begin_with_preroll(uint32_t pre_roll_ms);
esp_err_t audio_capture_session_stop(void);
esp_err_t audio_capture_session_stop_with_origin(audio_capture_stop_origin_t origin);
esp_err_t audio_capture_session_cancel(void);
bool audio_capture_session_is_active(void);
uint32_t audio_capture_get_frame_count(void);
uint32_t audio_capture_get_dropped_frame_count(void);
void audio_capture_set_voice_activity_handler(
    audio_capture_voice_activity_handler_t handler);
esp_err_t audio_capture_set_voice_activation_monitoring(bool enabled);
bool audio_capture_voice_activation_monitoring_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
