#include "battery_monitor.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_pins.h"

#define BATTERY_MONITOR_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_MONITOR_ADC_RAW_MAX 4095U
#define BATTERY_MONITOR_ADC_FALLBACK_REF_MV 3300U
#define BATTERY_MONITOR_DIVIDER_NUMERATOR 2U
#define BATTERY_MONITOR_DIVIDER_DENOMINATOR 1U
#define BATTERY_MONITOR_ABSOLUTE_MIN_MV 2700U
#define BATTERY_MONITOR_EMPTY_MV 2800U
#define BATTERY_MONITOR_FULL_MV 4200U
#define BATTERY_MONITOR_SAMPLE_COUNT 4U
#define BATTERY_MONITOR_ADC_DISCARD_COUNT 3U
#define BATTERY_MONITOR_ADC_SETTLE_US 300U
#define BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE_NUMERATOR 1010U
#define BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE_DENOMINATOR 1000U
#define BATTERY_MONITOR_V2_CURRENT_MA_PER_ADC_MV 2U

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
static portMUX_TYPE s_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static battery_monitor_adc_channel_state_t s_battery_adc = {
    .gpio = BOARD_PINS_BAT_V_ADC_IO,
};
static battery_monitor_adc_channel_state_t s_3v3_current_adc = {
    .gpio = BOARD_PINS_TPS63020_I_ADC_IO,
};
static battery_monitor_adc_channel_state_t s_led_current_adc = {
    .gpio = BOARD_PINS_SY7088_I_ADC_IO,
};
static battery_monitor_power_rail_status_t s_cached_3v3_power_rail;
static battery_monitor_power_rail_status_t s_cached_led_power_rail;
static bool s_cached_3v3_power_rail_valid;
static bool s_cached_led_power_rail_valid;
static uint32_t s_power_rail_sequence;

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
    if (state->gpio == GPIO_NUM_NC || state->gpio < 0 || state->gpio >= GPIO_NUM_MAX) {
        return ESP_ERR_NOT_SUPPORTED;
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
                "ADC pin unavailable: gpio=%d unit=%d ret=%s",
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
            "ADC channel config failed: gpio=%d channel=%d ret=%s",
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
        "ADC ready: gpio=%d unit=%d channel=%d calibrated=%u",
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

    esp_rom_delay_us(BATTERY_MONITOR_ADC_SETTLE_US);
    for (uint8_t i = 0; i < BATTERY_MONITOR_ADC_DISCARD_COUNT; ++i) {
        int discard_raw = 0;
        ret = adc_oneshot_read(s_adc1_handle, state->channel, &discard_raw);
        if (ret != ESP_OK) {
            return ret;
        }
        esp_rom_delay_us(BATTERY_MONITOR_ADC_SETTLE_US);
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

static int battery_monitor_apply_source_impedance_correction(int pad_mv)
{
    if (pad_mv <= 0) {
        return pad_mv;
    }

    return (int)((((uint32_t)pad_mv * BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE_NUMERATOR) +
                  (BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE_DENOMINATOR / 2U)) /
                 BATTERY_MONITOR_ADC_SOURCE_IMPEDANCE_DENOMINATOR);
}

static uint32_t battery_monitor_battery_mv_from_pad_mv(int corrected_pad_mv)
{
    if (corrected_pad_mv <= 0) {
        return 0U;
    }

    return ((uint32_t)corrected_pad_mv * BATTERY_MONITOR_DIVIDER_NUMERATOR) /
           BATTERY_MONITOR_DIVIDER_DENOMINATOR;
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

static esp_err_t battery_monitor_ensure_mutex(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    SemaphoreHandle_t new_mutex = xSemaphoreCreateMutex();
    if (new_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&s_mutex_init_lock);
    if (s_mutex == NULL) {
        s_mutex = new_mutex;
        new_mutex = NULL;
    }
    portEXIT_CRITICAL(&s_mutex_init_lock);

    if (new_mutex != NULL) {
        vSemaphoreDelete(new_mutex);
    }
    return ESP_OK;
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

    esp_err_t mutex_ret = battery_monitor_ensure_mutex();
    if (mutex_ret != ESP_OK) {
        out_status->result = mutex_ret;
        return mutex_ret;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        out_status->result = ESP_ERR_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }

    int raw = 0;
    int measured_pad_mv = 0;
    bool calibrated = false;
    uint8_t sample_count = 0;
    esp_err_t ret = battery_monitor_read_adc_locked(
        &s_battery_adc,
        &raw,
        &measured_pad_mv,
        &calibrated,
        &sample_count);
    if (ret != ESP_OK) {
        out_status->result = ret;
        xSemaphoreGive(s_mutex);
        return ret;
    }

    int corrected_pad_mv =
        battery_monitor_apply_source_impedance_correction(measured_pad_mv);
    uint32_t battery_mv = battery_monitor_battery_mv_from_pad_mv(corrected_pad_mv);

    out_status->valid = true;
    out_status->voltage_mv = battery_mv;
    out_status->level_percent = battery_monitor_percent_from_mv(battery_mv);
    out_status->raw_adc = raw;
    out_status->adc_raw_mv = measured_pad_mv;
    out_status->adc_mv = corrected_pad_mv;
    out_status->adc_correction_mv = corrected_pad_mv - measured_pad_mv;
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
        return "TPS63020_input_branch";
    case BATTERY_MONITOR_POWER_RAIL_LED_5V:
        return "SY7088_input_branch";
    default:
        return "unknown";
    }
}

static battery_monitor_power_rail_status_t *battery_monitor_power_rail_cache(
    battery_monitor_power_rail_t rail,
    bool **out_valid)
{
    if (out_valid == NULL) {
        return NULL;
    }

    switch (rail) {
    case BATTERY_MONITOR_POWER_RAIL_3V3:
        *out_valid = &s_cached_3v3_power_rail_valid;
        return &s_cached_3v3_power_rail;
    case BATTERY_MONITOR_POWER_RAIL_LED_5V:
        *out_valid = &s_cached_led_power_rail_valid;
        return &s_cached_led_power_rail;
    default:
        *out_valid = NULL;
        return NULL;
    }
}

static void battery_monitor_store_power_rail_cache_locked(
    battery_monitor_power_rail_t rail,
    battery_monitor_power_rail_status_t *status)
{
    bool *valid = NULL;
    battery_monitor_power_rail_status_t *cache =
        battery_monitor_power_rail_cache(rail, &valid);
    if (cache == NULL || valid == NULL || status == NULL || !status->valid) {
        return;
    }

    status->sequence = ++s_power_rail_sequence;
    *cache = *status;
    *valid = true;
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
    out_status->calibration_status = "ina180a2_10mR_nominal";
    out_status->current_telemetry_present = false;
    out_status->gpio = -1;
    out_status->nominal_rail_mv = 0;
    out_status->rail_voltage_provisional = false;
    out_status->current_calibrated = false;
    out_status->current_ma_valid = false;
    out_status->power_mw_valid = false;
    out_status->result = ESP_FAIL;

    battery_monitor_adc_channel_state_t *adc = battery_monitor_power_rail_adc(rail);
    if (adc == NULL) {
        out_status->result = ESP_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    out_status->gpio = (int32_t)adc->gpio;
    out_status->current_telemetry_present =
        adc->gpio != GPIO_NUM_NC && adc->gpio >= 0 && adc->gpio < GPIO_NUM_MAX;
    if (!out_status->current_telemetry_present) {
        out_status->calibration_status = "current_telemetry_not_populated";
        out_status->result = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t mutex_ret = battery_monitor_ensure_mutex();
    if (mutex_ret != ESP_OK) {
        out_status->result = mutex_ret;
        return mutex_ret;
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
        int battery_raw = 0;
        int battery_pad_mv = 0;
        bool battery_calibrated = false;
        uint8_t battery_sample_count = 0;
        esp_err_t battery_ret = battery_monitor_read_adc_locked(
            &s_battery_adc,
            &battery_raw,
            &battery_pad_mv,
            &battery_calibrated,
            &battery_sample_count);
        int corrected_battery_pad_mv =
            battery_monitor_apply_source_impedance_correction(battery_pad_mv);
        uint32_t battery_mv = battery_ret == ESP_OK
            ? battery_monitor_battery_mv_from_pad_mv(corrected_battery_pad_mv)
            : 0U;
        int32_t current_ma = (int32_t)((uint32_t)pad_mv * BATTERY_MONITOR_V2_CURRENT_MA_PER_ADC_MV);

        out_status->valid = true;
        out_status->raw_adc = raw;
        out_status->adc_mv = pad_mv;
        out_status->adc_calibrated = calibrated;
        out_status->current_calibrated = calibrated;
        out_status->current_ma_valid = true;
        out_status->estimated_current_ma = current_ma;
        out_status->nominal_rail_mv = battery_mv;
        out_status->power_mw_valid = battery_ret == ESP_OK;
        out_status->estimated_power_mw = battery_ret == ESP_OK
            ? (int32_t)(((uint64_t)(uint32_t)current_ma * (uint64_t)battery_mv) / 1000ULL)
            : 0;
        out_status->sample_count = sample_count;
        out_status->calibration_status = battery_ret == ESP_OK && battery_calibrated
            ? "ina180a2_10mR_adc_calibrated_battery_adc_calibrated"
            : "ina180a2_10mR_nominal_battery_mv_reconstructed";
        battery_monitor_store_power_rail_cache_locked(rail, out_status);
    }
    xSemaphoreGive(s_mutex);
    return ret;
}

bool battery_monitor_get_cached_power_rail(
    battery_monitor_power_rail_t rail,
    battery_monitor_power_rail_status_t *out_status)
{
    if (out_status == NULL) {
        return false;
    }

    esp_err_t mutex_ret = battery_monitor_ensure_mutex();
    if (mutex_ret != ESP_OK) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool *valid = NULL;
    battery_monitor_power_rail_status_t *cache =
        battery_monitor_power_rail_cache(rail, &valid);
    bool found = cache != NULL && valid != NULL && *valid;
    if (found) {
        *out_status = *cache;
    }
    xSemaphoreGive(s_mutex);
    return found;
}
