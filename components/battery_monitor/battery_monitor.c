#include "battery_monitor.h"

#include <stdbool.h>
#include <stdint.h>

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
#define BATTERY_MONITOR_EMPTY_MV 3000U
#define BATTERY_MONITOR_FULL_MV 4200U

static const char *TAG = "battery_monitor";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static adc_unit_t s_adc_unit = ADC_UNIT_1;
static adc_channel_t s_adc_channel = ADC_CHANNEL_6;
static bool s_adc_ready;
static bool s_cali_ready;
static bool s_warned;
static SemaphoreHandle_t s_mutex;

static bool battery_monitor_calibration_init(adc_unit_t unit, adc_channel_t channel)
{
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = BATTERY_MONITOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&curve_config, &s_cali_handle);
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
    ret = adc_cali_create_scheme_line_fitting(&line_config, &s_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "battery ADC calibration: line fitting");
        return true;
    }
#endif

    ESP_LOGW(TAG, "battery ADC calibration unavailable: %s", esp_err_to_name(ret));
    return false;
}

static esp_err_t battery_monitor_adc_init(void)
{
    if (s_adc_ready) {
        return ESP_OK;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    esp_err_t ret = adc_oneshot_io_to_channel((int)BOARD_PINS_BAT_V_ADC_IO, &unit, &channel);
    if (ret != ESP_OK) {
        if (!s_warned) {
            ESP_LOGW(
                TAG,
                "battery ADC pin unavailable: gpio=%d ret=%s",
                (int)BOARD_PINS_BAT_V_ADC_IO,
                esp_err_to_name(ret));
            s_warned = true;
        }
        return ret;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (ret != ESP_OK) {
        if (!s_warned) {
            ESP_LOGW(TAG, "battery ADC unit init failed: %s", esp_err_to_name(ret));
            s_warned = true;
        }
        return ret;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_MONITOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC channel config failed: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

    s_adc_unit = unit;
    s_adc_channel = channel;
    s_cali_ready = battery_monitor_calibration_init(unit, channel);
    s_adc_ready = true;
    ESP_LOGI(
        TAG,
        "battery ADC ready: gpio=%d unit=%d channel=%d",
        (int)BOARD_PINS_BAT_V_ADC_IO,
        (int)s_adc_unit,
        (int)s_adc_channel);
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

    esp_err_t ret = battery_monitor_adc_init();
    if (ret != ESP_OK) {
        out_status->result = ret;
        xSemaphoreGive(s_mutex);
        return ret;
    }

    int raw = 0;
    ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
    if (ret != ESP_OK) {
        out_status->result = ret;
        xSemaphoreGive(s_mutex);
        return ret;
    }

    int pad_mv = 0;
    if (s_cali_ready) {
        ret = adc_cali_raw_to_voltage(s_cali_handle, raw, &pad_mv);
        if (ret != ESP_OK) {
            out_status->result = ret;
            xSemaphoreGive(s_mutex);
            return ret;
        }
    } else {
        pad_mv = (int)(((uint32_t)raw * BATTERY_MONITOR_ADC_FALLBACK_REF_MV) /
                       BATTERY_MONITOR_ADC_RAW_MAX);
    }

    uint32_t battery_mv =
        ((uint32_t)pad_mv * BATTERY_MONITOR_DIVIDER_NUMERATOR) /
        BATTERY_MONITOR_DIVIDER_DENOMINATOR;

    out_status->valid = true;
    out_status->voltage_mv = battery_mv;
    out_status->level_percent = battery_monitor_percent_from_mv(battery_mv);
    out_status->raw_adc = raw;
    out_status->result = ESP_OK;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
