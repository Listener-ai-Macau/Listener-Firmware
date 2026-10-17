#pragma once

#include <stdint.h>

/* The export worker initializes and owns this optional precise blocker.
 * Failures and other callers retain the original coarse-tick blocking path. */
void ble_audio_cooperative_wait_init(void);
void ble_audio_cooperative_wait_ms(uint32_t delay_ms);
