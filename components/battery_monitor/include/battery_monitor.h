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
    int adc_raw_mv;
    int adc_driver_mv;
    int adc_mv;
    int adc_correction_mv;
    int adc_trim_mv;
    bool adc_trim_valid;
    esp_err_t adc_trim_result;
    bool adc_calibrated;
    uint8_t sample_count;
    esp_err_t result;
} battery_monitor_status_t;

typedef enum {
    BATTERY_MONITOR_POWER_RAIL_3V3 = 0,
    BATTERY_MONITOR_POWER_RAIL_LED_5V,
} battery_monitor_power_rail_t;

typedef struct {
    bool valid;
    const char *rail_name;
    const char *calibration_status;
    bool current_telemetry_present;
    int32_t gpio;
    uint32_t nominal_rail_mv;
    bool rail_voltage_provisional;
    int raw_adc;
    int adc_mv;
    bool adc_calibrated;
    bool current_calibrated;
    bool current_ma_valid;
    int32_t estimated_current_ma;
    bool power_mw_valid;
    int32_t estimated_power_mw;
    uint8_t sample_count;
    uint32_t sequence;
    esp_err_t result;
} battery_monitor_power_rail_status_t;

esp_err_t battery_monitor_read(battery_monitor_status_t *out_status);
esp_err_t battery_monitor_read_power_rail(
    battery_monitor_power_rail_t rail,
    battery_monitor_power_rail_status_t *out_status);
bool battery_monitor_get_cached_power_rail(
    battery_monitor_power_rail_t rail,
    battery_monitor_power_rail_status_t *out_status);
uint8_t battery_monitor_percent_from_mv(uint32_t battery_mv);
esp_err_t battery_monitor_calibrate_adc_trim_from_dmm_mv(
    int dmm_pad_mv,
    battery_monitor_status_t *out_status);
esp_err_t battery_monitor_clear_adc_trim(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MONITOR_H */
