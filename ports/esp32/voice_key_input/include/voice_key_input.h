#ifndef VOICE_KEY_INPUT_H
#define VOICE_KEY_INPUT_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_key_input_start(void);
bool voice_key_input_take_toggle_event(void);
bool voice_key_input_take_recovery_event(void);
const char *voice_key_input_get_active_source(void);
esp_err_t voice_key_input_set_recording_output(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
