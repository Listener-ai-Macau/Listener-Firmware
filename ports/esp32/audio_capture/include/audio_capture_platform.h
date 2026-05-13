#ifndef AUDIO_CAPTURE_PLATFORM_H
#define AUDIO_CAPTURE_PLATFORM_H

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

i2c_master_bus_handle_t audio_capture_get_i2c_bus_handle(void);

#ifdef __cplusplus
}
#endif

#endif
