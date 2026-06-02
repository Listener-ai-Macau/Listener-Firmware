#ifndef VOICE_RECORDING_CONTROL_H
#define VOICE_RECORDING_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_recording_control_start(void);
esp_err_t voice_recording_control_dispatch_control_command(const char *command, const char *source);
bool voice_recording_control_consume_usb_control_byte(uint8_t input_char);
uint32_t voice_recording_control_get_session_count(void);

#ifdef __cplusplus
}
#endif

#endif
