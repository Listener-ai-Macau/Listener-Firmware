#ifndef VOICE_RECORDING_CONTROL_H
#define VOICE_RECORDING_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_recording_control_start(void);
bool voice_recording_control_consume_usb_control_byte(uint8_t input_char);

#ifdef __cplusplus
}
#endif

#endif
