#include "battery_monitor.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_pins.h"

#define BATTERY_MONITOR_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_MONITOR_ADC_RAW_MAX 4095U
#define BATTERY_MONITOR_ADC_FALLBACK_REF_MV 3300U
#define BATTERY_MONITOR_DIVIDER_NUMERATOR 2U
#define BATTERY_MONITOR_DIVIDER_DENOMINATOR 1U
#define BATTERY_MONITOR_EMPTY_MV 2700U
#define BATTERY_MONITOR_FULL_MV 4200U
#define BATTERY_MONITOR_SAMPLE_COUNT 4U
#define BATTERY_MONITOR_V2_3V3_NOMINAL_MV 3300U
#define BATTERY_MONITOR_V2_LED_NOMINAL_MV 5000U

static const char *TAG = "battery_monitor";

typedef struct {
    gpio_num_t gpio;
    adc_unit_t unit;
    adc_channel_t channel;
    bool mapped;
    bool configured;
    bool cali_ready;
    adc_cali_handle_t cali_handle;
} battery_monitor_adc_channel_state_t;

static adc_oneshot_unit_handle_t s_adc1_handle;
static bool s_warned;
static SemaphoreHandle_t s_mutex;
static battery_monitor_adc_channel_state_t s_battery_adc = {
    .gpio = BOARD_PINS_BAT_V_ADC_IO,
};
static battery_monitor_adc_channel_state_t s_3v3_current_adc = {
    .gpio = BOARD_PINS_TPS63020_I_ADC_IO,
};
static battery_monitor_adc_channel_state_t s_led_current_adc = {
    .gpio = BOARD_PINS_SY7088_I_ADC_IO,
};

static bool battery_monitor_calibration_init(
    adc_unit_t unit,
    adc_channel_t channel,
    adc_cali_handle_t *out_handle)
{
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = BATTERY_MONITOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&curve_config, out_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "battery ADC calibration: curve fitting");
        return true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = unit,
        .atten = BATTERY_MONITOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&line_config, out_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "battery ADC calibration: line fitting");
        return true;
    }
#endif

    ESP_LOGW(TAG, "battery ADC calibration unavailable: %s", esp_err_to_name(ret));
    return false;
}

static esp_err_t battery_monitor_adc1_init(void)
{
    if (s_adc1_handle != NULL) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_config, &s_adc1_handle);
    if (ret != ESP_OK) {
        if (!s_warned) {
            ESP_LOGW(TAG, "battery ADC unit init failed: %s", esp_err_to_name(ret));
            s_warned = true;
        }
        return ret;
    }

    return ESP_OK;
}

static esp_err_t battery_monitor_adc_channel_init(battery_monitor_adc_channel_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->configured) {
        return ESP_OK;
    }

    esp_err_t ret = battery_monitor_adc1_init();
    if (ret != ESP_OK) {
        return ret;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    ret = adc_oneshot_io_to_channel((int)state->gpio, &unit, &channel);
    if (ret != ESP_OK || unit != ADC_UNIT_1) {
        if (!s_warned) {
            ESP_LOGW(
                TAG,
                "V2 ADC pin unavailable: gpio=%d unit=%d ret=%s",
                (int)state->gpio,
                (int)unit,
                esp_err_to_name(ret));
            s_warned = true;
        }
        return ret == ESP_OK ? ESP_ERR_NOT_SUPPORTED : ret;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_MONITOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc1_handle, channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "V2 ADC channel config failed: gpio=%d channel=%d ret=%s",
            (int)state->gpio,
            (int)channel,
            esp_err_to_name(ret));
        return ret;
    }

    state->unit = unit;
    state->channel = channel;
    state->mapped = true;
    state->cali_ready = battery_monitor_calibration_init(unit, channel, &state->cali_handle);
    state->configured = true;
    ESP_LOGI(
        TAG,
        "V2 ADC ready: gpio=%d unit=%d channel=%d calibrated=%u",
        (int)state->gpio,
        (int)state->unit,
        (int)state->channel,
        state->cali_ready ? 1u : 0u);
    return ESP_OK;
}

static esp_err_t battery_monitor_read_adc_locked(
    battery_monitor_adc_channel_state_t *state,
    int *out_raw,
    int *out_adc_mv,
    bool *out_calibrated,
    uint8_t *out_sample_count)
{
    if (state == NULL || out_raw == NULL || out_adc_mv == NULL ||
        out_calibrated == NULL || out_sample_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = battery_monitor_adc_channel_init(state);
    if (ret != ESP_OK) {
        return ret;
    }

    int raw_total = 0;
    int mv_total = 0;
    uint8_t sample_count = 0;
    for (uint8_t i = 0; i < BATTERY_MONITOR_SAMPLE_COUNT; ++i) {
        int raw = 0;
        ret = adc_oneshot_read(s_adc1_handle, state->channel, &raw);
        if (ret != ESP_OK) {
            return ret;
        }

        int pad_mv = 0;
        if (state->cali_ready) {
            ret = adc_cali_raw_to_voltage(state->cali_handle, raw, &pad_mv);
            if (ret != ESP_OK) {
                return ret;
            }
        } else {
            pad_mv = (int)(((uint32_t)raw * BATTERY_MONITOR_ADC_FALLBACK_REF_MV) /
                           BATTERY_MONITOR_ADC_RAW_MAX);
        }

        raw_total += raw;
        mv_total += pad_mv;
        sample_count++;
    }

    *out_raw = sample_count > 0 ? raw_total / sample_count : 0;
    *out_adc_mv = sample_count > 0 ? mv_total / sample_count : 0;
    *out_calibrated = state->cali_ready;
    *out_sample_count = sample_count;
    return ESP_OK;
}

uint8_t battery_monitor_percent_from_mv(uint32_t battery_mv)
{
    if (battery_mv <= BATTERY_MONITOR_EMPTY_MV) {
        return 0;
    }
    if (battery_mv >= BATTERY_MONITOR_FULL_MV) {
        return 100;
    }

    uint32_t range_mv = BATTERY_MONITOR_FULL_MV - BATTERY_MONITOR_EMPTY_MV;
    uint32_t level =
        ((battery_mv - BATTERY_MONITOR_EMPTY_MV) * 100U + (range_mv / 2U)) /
        range_mv;
    return (uint8_t)level;
}

esp_err_t battery_monitor_read(battery_monitor_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_status = (battery_monitor_status_t){
        .valid = false,
        .result = ESP_FAIL,
    };

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            out_status->result = ESP_ERR_NO_MEM;
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        out_status->result = ESP_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    int raw = 0;
    int pad_mv = 0;
    bool calibrated = false;
    uint8_t sample_count = 0;
    esp_err_t ret = battery_monitor_read_adc_locked(
        &s_battery_adc,
        &raw,
        &pad_mv,
        &calibrated,
        &sample_count);
    if (ret != ESP_OK) {
        out_status->result = ret;
        xSemaphoreGive(s_mutex);
        return ret;
    }

    uint32_t battery_mv =
        ((uint32_t)pad_mv * BATTERY_MONITOR_DIVIDER_NUMERATOR) /
        BATTERY_MONITOR_DIVIDER_DENOMINATOR;

    out_status->valid = true;
    out_status->voltage_mv = battery_mv;
    out_status->level_percent = battery_monitor_percent_from_mv(battery_mv);
    out_status->raw_adc = raw;
    out_status->adc_mv = pad_mv;
    out_status->adc_calibrated = calibrated;
    out_status->sample_count = sample_count;
    out_status->result = ESP_OK;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static battery_monitor_adc_channel_state_t *battery_monitor_power_rail_adc(
    battery_monitor_power_rail_t rail)
{
    switch (rail) {
    case BATTERY_MONITOR_POWER_RAIL_3V3:
        return &s_3v3_current_adc;
    case BATTERY_MONITOR_POWER_RAIL_LED_5V:
        return &s_led_current_adc;
    default:
        return NULL;
    }
}

static const char *battery_monitor_power_rail_name(battery_monitor_power_rail_t rail)
{
    switch (rail) {
    case BATTERY_MONITOR_POWER_RAIL_3V3:
        return "TPS63020_3V3";
    case BATTERY_MONITOR_POWER_RAIL_LED_5V:
        return "SY7088_LED_5V";
    default:
        return "unknown";
    }
}

static uint32_t battery_monitor_power_rail_nominal_mv(battery_monitor_power_rail_t rail)
{
    switch (rail) {
    case BATTERY_MONITOR_POWER_RAIL_3V3:
        return BATTERY_MONITOR_V2_3V3_NOMINAL_MV;
    case BATTERY_MONITOR_POWER_RAIL_LED_5V:
        return BATTERY_MONITOR_V2_LED_NOMINAL_MV;
    default:
        return 0;
    }
}

esp_err_t battery_monitor_read_power_rail(
    battery_monitor_power_rail_t rail,
    battery_monitor_power_rail_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->valid = false;
    out_status->rail_name = battery_monitor_power_rail_name(rail);
    out_status->calibration_status = "uncalibrated";
    out_status->nominal_rail_mv = battery_monitor_power_rail_nominal_mv(rail);
    out_status->rail_voltage_provisional = true;
    out_status->current_calibrated = false;
    out_status->current_ma_valid = false;
    out_status->power_mw_valid = false;
    out_status->result = ESP_FAIL;

    battery_monitor_adc_channel_state_t *adc = battery_monitor_power_rail_adc(rail);
    if (adc == NULL) {
        out_status->result = ESP_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    out_status->gpio = (uint32_t)adc->gpio;

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            out_status->result = ESP_ERR_NO_MEM;
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        out_status->result = ESP_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    int raw = 0;
    int pad_mv = 0;
    bool calibrated = false;
    uint8_t sample_count = 0;
    esp_err_t ret = battery_monitor_read_adc_locked(
        adc,
        &raw,
        &pad_mv,
        &calibrated,
        &sample_count);

    out_status->result = ret;
    if (ret == ESP_OK) {
        out_status->valid = true;
        out_status->raw_adc = raw;
        out_status->adc_mv = pad_mv;
        out_status->adc_calibrated = calibrated;
        out_status->sample_count = sample_count;
    }
    xSemaphoreGive(s_mutex);
    return ret;
}
