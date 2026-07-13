#ifndef VOICE_KEY_INPUT_H
#define VOICE_KEY_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_key_input_start(void);
bool voice_key_input_take_toggle_event(void);
bool voice_key_input_take_recovery_event(void);
void voice_key_input_set_recording_control_task(TaskHandle_t task_handle);
bool voice_key_input_take_fast_idle_recording_event(uint32_t *out_press_to_dispatch_ms);
bool voice_key_input_take_fast_idle_recording_cancel_event(void);
void voice_key_input_complete_fast_idle_recording_event(bool suppress_fallback_hid);
const char *voice_key_input_get_active_source(void);
bool voice_key_input_ec11_press_suppresses_rotation(void);
esp_err_t voice_key_input_set_recording_output(bool enabled);
esp_err_t voice_key_input_enqueue_generated_single_click(void);
esp_err_t voice_key_input_enqueue_generated_double_click(void);

#ifdef __cplusplus
}
#endif

#endif
