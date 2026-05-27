#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t system_health_init(void);
esp_err_t system_health_start(void);
void system_health_set_low_power_mode(bool enabled);

#endif /* SYSTEM_HEALTH_H */
