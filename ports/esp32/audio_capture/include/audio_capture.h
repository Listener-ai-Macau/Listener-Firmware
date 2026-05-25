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
esp_err_t audio_capture_session_begin(void);
esp_err_t audio_capture_session_stop(void);
esp_err_t audio_capture_session_cancel(void);
bool audio_capture_session_is_active(void);
uint32_t audio_capture_get_frame_count(void);
uint32_t audio_capture_get_dropped_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif
