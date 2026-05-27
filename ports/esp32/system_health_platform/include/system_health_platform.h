#ifndef SYSTEM_HEALTH_PLATFORM_H
#define SYSTEM_HEALTH_PLATFORM_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t system_health_platform_init(void);
esp_err_t system_health_platform_start(void);
void system_health_platform_set_low_power_mode(bool enabled);

#endif /* SYSTEM_HEALTH_PLATFORM_H */
