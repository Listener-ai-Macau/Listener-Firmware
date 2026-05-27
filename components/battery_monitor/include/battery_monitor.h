#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    uint32_t voltage_mv;
    uint8_t level_percent;
    int raw_adc;
    esp_err_t result;
} battery_monitor_status_t;

esp_err_t battery_monitor_read(battery_monitor_status_t *out_status);
uint8_t battery_monitor_percent_from_mv(uint32_t battery_mv);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MONITOR_H */
