#ifndef AUDIO_CAPTURE_PLATFORM_H
#define AUDIO_CAPTURE_PLATFORM_H

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
#include "driver/i2c_master.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
i2c_master_bus_handle_t audio_capture_get_i2c_bus_handle(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
