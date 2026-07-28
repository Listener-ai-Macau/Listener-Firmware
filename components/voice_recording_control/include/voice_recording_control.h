#ifndef VOICE_RECORDING_CONTROL_H
#define VOICE_RECORDING_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_recording_control_start(void);
/* Safe-mode / degraded boot: EC11 double-click re-pair + USB recovery without mic capture. */
esp_err_t voice_recording_control_start_recovery_only(void);
/*
 * Start mic capture after NimBLE host is up. Boot must not start AFE/I2S before
 * ble_hid_start — that left internal_free~3KB and panicked NimBLE
 * (ble_hs_event_start_stage2 rc!=0) into safe_mode.
 */
esp_err_t voice_recording_control_enable_audio(void);
esp_err_t voice_recording_control_dispatch_control_command(const char *command, const char *source);
bool voice_recording_control_consume_usb_control_byte(uint8_t input_char);
uint32_t voice_recording_control_get_session_count(void);

#ifdef __cplusplus
}
#endif

#endif
