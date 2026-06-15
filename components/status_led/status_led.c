#include "status_led.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "battery_monitor.h"
#include "board_pins.h"
#include "device_settings.h"
#include "diag_log.h"
#include "power_manager.h"
#include "status_led_strip_backend.h"

#define STATUS_LED_STATUS_COUNT 6
#define STATUS_LED_EC11_COUNT 12
#define STATUS_LED_KEY_COUNT 4
#define STATUS_LED_EDGE_COUNT 6
#define STATUS_LED_STRIP_COUNT 4
#define STATUS_LED_MAX_STRIP_COUNT STATUS_LED_EC11_COUNT

#define STATUS_LED_TASK_STACK_BYTES (5 * 1024)
#define STATUS_LED_REFRESH_MS 50U
#define STATUS_LED_IDLE_REFRESH_MS 1000U
#define STATUS_LED_TX_MUTEX_WAIT_MS 100
#define STATUS_LED_POWER_POLL_MS 5000U
#define STATUS_LED_STATUS_WINDOW_MS 6000U
#define STATUS_LED_BOOT_ACK_MS 2500U
#define STATUS_LED_BLE_CONFIDENCE_MS 8000U
#define STATUS_LED_OOBE_CONFIDENCE_MS 25000U
#define STATUS_LED_ERROR_HOLD_MS 6000U
#define STATUS_LED_OK_TOTAL_MS 900U
#define STATUS_LED_OK_PEAK_MS 160U
#define STATUS_LED_KEY_FEEDBACK_MS 240U
#define STATUS_LED_CHARGING_BREATH_PERIOD_MS 1400U
#define STATUS_LED_CHARGING_BREATH_MIN_PERCENT 12U
#define STATUS_LED_CHARGING_BREATH_MAX_PERCENT 100U
#define STATUS_LED_CHARGE_FULL_DEBOUNCE_MS 10000U
#define STATUS_LED_CHARGE_FULL_MIN_MV 4050U
#define STATUS_LED_CHARGE_FULL_MIN_PERCENT 88U
#define STATUS_LED_FULL_STEADY_PERCENT 100U
#define STATUS_LED_FULL_STATUS_STEADY_PERCENT 100U
#define STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS 120U
#define STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS 7880U
#define STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT 18U
#define STATUS_LED_FULL_BRIGHTNESS_PERCENT 100U
#define STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA 2000U
#define STATUS_LED_LOW_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_STANDARD_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT 100U
#define STATUS_LED_LOW_PROFILE_BUDGET_MA 300U
#define STATUS_LED_STANDARD_PROFILE_BUDGET_MA 760U
#define STATUS_LED_AMBIENT_PROFILE_BUDGET_MA 620U
#define STATUS_LED_CHASE_DEFAULT_STEP_MS 250U
#define STATUS_LED_CONTRACT_REV "status_key_isolated_charge_high_contrast_v11"
#define STATUS_LED_NVS_NAMESPACE "status_led"
#define STATUS_LED_NVS_PROFILE_KEY "profile"
#define STATUS_LED_NVS_BRIGHTNESS_KEY "brightness"
#define STATUS_LED_NVS_STATUS_ORDER_KEY "ord_status"
#define STATUS_LED_NVS_EC11_ORDER_KEY "ord_ec11"
#define STATUS_LED_NVS_KEY_ORDER_KEY "ord_key"
#define STATUS_LED_NVS_EDGE_ORDER_KEY "ord_edge"
#define STATUS_LED_USB_PREFIX "LED:"
#define STATUS_LED_STATUS_FIRST_LED 1U
#define STATUS_LED_EC11_FIRST_LED 7U
#define STATUS_LED_KEY_FIRST_LED 11U
#define STATUS_LED_EDGE_FIRST_LED 17U
#define STATUS_LED_STATUS_PHYSICAL_MAP "LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN"
#define STATUS_LED_KEY_PHYSICAL_MAP "LED11:KEY1,LED12:KEY2,LED13:KEY3,LED14:KEY4"
#define STATUS_LED_STATUS_KEY_MAPPING_CONTRACT "status=LED1..LED6,key=LED11..LED14"
#define STATUS_LED_REC_GOLD_R 255U
#define STATUS_LED_REC_GOLD_G 172U
#define STATUS_LED_REC_GOLD_B 0U
#define STATUS_LED_RECORDING_BREATH_PERIOD_MS 1900U
#define STATUS_LED_RECORDING_BREATH_MIN_PERCENT 24U
#define STATUS_LED_RECORDING_BREATH_MAX_PERCENT 42U
#define STATUS_LED_RECORDING_LEVEL_STALE_MS 300U
#define STATUS_LED_RECORDING_LEVEL_BOOST_MULTIPLIER 4U
#define STATUS_LED_RECORDING_LEVEL_FLOOR_PERCENT 35U
#define STATUS_LED_RECORDING_LEVEL_RANGE_PERCENT 65U

typedef enum {
    STATUS_LED_STRIP_STATUS = 0,
    STATUS_LED_STRIP_EC11,
    STATUS_LED_STRIP_KEY,
    STATUS_LED_STRIP_EDGE,
} status_led_strip_id_t;

typedef enum {
    STATUS_LED_PROFILE_OFF = 0,
    STATUS_LED_PROFILE_LOW,
    STATUS_LED_PROFILE_STANDARD,
    STATUS_LED_PROFILE_AMBIENT,
    STATUS_LED_PROFILE_FACTORY,
} status_led_profile_t;

#define STATUS_LED_STATUS_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB
#define STATUS_LED_KEY_DEFAULT_COLOR_ORDER STATUS_LED_COLOR_ORDER_GRB

typedef enum {
    STATUS_LED_TEST_NONE = 0,
    STATUS_LED_TEST_RGBW,
    STATUS_LED_TEST_MAP,
    STATUS_LED_TEST_CHASE,
    STATUS_LED_TEST_PIXEL,
} status_led_test_mode_t;

typedef enum {
    STATUS_LED_SEM_PWR = 0,
    STATUS_LED_SEM_BLE,
    STATUS_LED_SEM_REC,
    STATUS_LED_SEM_AI,
    STATUS_LED_SEM_OK,
    STATUS_LED_SEM_WARN,
} status_led_semantic_t;

typedef struct {
    status_led_rgb_t status[STATUS_LED_STATUS_COUNT];
    status_led_rgb_t ec11[STATUS_LED_EC11_COUNT];
    status_led_rgb_t key[STATUS_LED_KEY_COUNT];
    status_led_rgb_t edge[STATUS_LED_EDGE_COUNT];
} status_led_frame_t;

typedef struct {
    const char *name;
    gpio_num_t gpio;
    uint8_t led_count;
    status_led_color_order_t color_order;
    status_led_strip_backend_t *backend;
} status_led_strip_t;

typedef struct {
    bool initialized;
    bool started;
    bool output_disabled;
    bool low_power_disabled;
    bool vdd_led_enable_assumed;
    bool ever_connected;
    status_led_profile_t profile;
    uint8_t brightness_percent;
    status_led_ble_state_t ble_state;
    status_led_rec_source_t rec_source;
    status_led_error_domain_t error_domain;
    status_led_error_severity_t error_severity;
    bool recording_active;
    bool processing_active;
    uint8_t recording_level_percent;
    bool battery_valid;
    bool external_power_present;
    bool charging;
    bool full;
    bool raw_charging;
    bool raw_full;
    bool charge_full_latched;
    uint8_t battery_level_percent;
    uint32_t battery_mv;
    uint32_t status_window_until_ms;
    uint32_t boot_feedback_until_ms;
    uint32_t ble_confidence_until_ms;
    uint32_t oobe_confidence_until_ms;
    uint32_t ok_started_ms;
    uint32_t ok_until_ms;
    uint32_t error_started_ms;
    uint32_t error_until_ms;
    uint32_t recording_level_updated_ms;
    uint32_t processing_started_ms;
    uint32_t last_transition_ms;
    uint32_t last_power_poll_ms;
    uint32_t charge_full_candidate_since_ms;
    uint32_t last_estimated_current_ma;
    uint32_t last_current_budget_ma;
    uint8_t last_budget_scale_percent;
    bool current_limited_by_budget;
    uint8_t key_pressed_mask;
    uint32_t key_until_ms[STATUS_LED_KEY_COUNT];
    status_led_test_mode_t test_mode;
    uint8_t test_strip_mask;
    uint32_t test_started_ms;
    uint16_t test_step_ms;
    status_led_strip_id_t test_pixel_strip;
    uint8_t test_pixel_index;
    status_led_rgb_t test_pixel_color;
    uint8_t test_pixel_percent;
    char last_reason[32];
    status_led_frame_t last_frame;
} status_led_state_t;

static const char *TAG = "status_led";

static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_tx_mutex;
static TaskHandle_t s_task_handle;
static status_led_state_t s_state;
static status_led_strip_t s_strips[STATUS_LED_STRIP_COUNT] = {
    {
        .name = "status",
        .gpio = BOARD_PINS_RGB_STATUS_IO,
        .led_count = STATUS_LED_STATUS_COUNT,
        .color_order = STATUS_LED_STATUS_DEFAULT_COLOR_ORDER,
    },
    {
        .name = "ec11",
        .gpio = BOARD_PINS_RGB_EC11_IO,
        .led_count = STATUS_LED_EC11_COUNT,
        .color_order = STATUS_LED_COLOR_ORDER_GRB,
    },
    {
        .name = "key",
        .gpio = BOARD_PINS_RGB_KEY_IO,
        .led_count = STATUS_LED_KEY_COUNT,
        .color_order = STATUS_LED_KEY_DEFAULT_COLOR_ORDER,
    },
    {
        .name = "edge",
        .gpio = BOARD_PINS_RGB_EDGE_IO,
        .led_count = STATUS_LED_EDGE_COUNT,
        .color_order = STATUS_LED_COLOR_ORDER_GRB,
    },
};

static void status_led_force_all_off(void);
static status_led_rgb_t status_led_boot_power_color_locked(void);

static uint32_t status_led_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static uint8_t status_led_clamp_u32_to_u8(uint32_t value)
{
    return value > UINT8_MAX ? UINT8_MAX : (uint8_t)value;
}

static status_led_rgb_t status_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    status_led_rgb_t color = {
        .r = r,
        .g = g,
        .b = b,
    };
    return color;
}

static bool status_led_rgb_is_on(status_led_rgb_t color)
{
    return color.r != 0U || color.g != 0U || color.b != 0U;
}

static bool status_led_strip_has_light(const status_led_rgb_t *colors, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (status_led_rgb_is_on(colors[index])) {
            return true;
        }
    }
    return false;
}

static status_led_rgb_t status_led_rec_gold(void)
{
    return status_led_rgb(
        STATUS_LED_REC_GOLD_R,
        STATUS_LED_REC_GOLD_G,
        STATUS_LED_REC_GOLD_B);
}

static uint8_t status_led_linear_percent_to_255(uint8_t percent)
{
    return (uint8_t)status_led_clamp_u32_to_u8(((uint32_t)percent * 255U + 50U) / 100U);
}

static status_led_rgb_t status_led_scale_raw(status_led_rgb_t color, uint8_t percent)
{
    status_led_rgb_t out = {0};
    const uint8_t gain = status_led_linear_percent_to_255(percent);
    out.r = status_led_clamp_u32_to_u8(((uint32_t)color.r * gain + 127U) / 255U);
    out.g = status_led_clamp_u32_to_u8(((uint32_t)color.g * gain + 127U) / 255U);
    out.b = status_led_clamp_u32_to_u8(((uint32_t)color.b * gain + 127U) / 255U);
    return out;
}

static status_led_rgb_t status_led_scale_gamma(status_led_rgb_t color, uint8_t percent)
{
    return status_led_scale_raw(color, percent);
}

static void status_led_set_max(status_led_rgb_t *slot, status_led_rgb_t color)
{
    if (slot->r < color.r) {
        slot->r = color.r;
    }
    if (slot->g < color.g) {
        slot->g = color.g;
    }
    if (slot->b < color.b) {
        slot->b = color.b;
    }
}

static const char *status_led_profile_name(status_led_profile_t profile)
{
    switch (profile) {
    case STATUS_LED_PROFILE_OFF: return "off";
    case STATUS_LED_PROFILE_LOW: return "low";
    case STATUS_LED_PROFILE_STANDARD: return "standard";
    case STATUS_LED_PROFILE_AMBIENT: return "ambient";
    case STATUS_LED_PROFILE_FACTORY: return "factory";
    default: return "unknown";
    }
}

static const char *status_led_ble_name(status_led_ble_state_t state)
{
    switch (state) {
    case STATUS_LED_BLE_DISCONNECTED: return "disconnected";
    case STATUS_LED_BLE_PAIRING: return "pairing";
    case STATUS_LED_BLE_RECONNECTING: return "reconnecting";
    case STATUS_LED_BLE_CONNECTED: return "connected";
    default: return "unknown";
    }
}

static const char *status_led_rec_source_name(status_led_rec_source_t source)
{
    switch (source) {
    case STATUS_LED_REC_SOURCE_NONE: return "none";
    case STATUS_LED_REC_SOURCE_DEVICE_MIC: return "device_mic";
    case STATUS_LED_REC_SOURCE_DESKTOP_MIC: return "desktop_mic";
    case STATUS_LED_REC_SOURCE_NOT_AVAILABLE: return "not_available";
    default: return "unknown";
    }
}

static const char *status_led_error_domain_name(status_led_error_domain_t domain)
{
    switch (domain) {
    case STATUS_LED_ERROR_DOMAIN_NONE: return "none";
    case STATUS_LED_ERROR_DOMAIN_BLE: return "ble";
    case STATUS_LED_ERROR_DOMAIN_REC: return "recording";
    case STATUS_LED_ERROR_DOMAIN_AI: return "ai";
    case STATUS_LED_ERROR_DOMAIN_OTA: return "ota";
    case STATUS_LED_ERROR_DOMAIN_POWER: return "power";
    case STATUS_LED_ERROR_DOMAIN_SYSTEM: return "system";
    default: return "unknown";
    }
}

static const char *status_led_error_severity_name(status_led_error_severity_t severity)
{
    switch (severity) {
    case STATUS_LED_ERROR_RETRYABLE: return "retryable";
    case STATUS_LED_ERROR_HARD: return "hard";
    default: return "unknown";
    }
}

static uint8_t status_led_profile_cap_percent_for(status_led_profile_t profile, bool safety)
{
    if (safety) {
        return STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    }
    switch (profile) {
    case STATUS_LED_PROFILE_OFF:
        return 0U;
    case STATUS_LED_PROFILE_LOW:
        return STATUS_LED_LOW_PROFILE_CAP_PERCENT;
    case STATUS_LED_PROFILE_AMBIENT:
        return STATUS_LED_AMBIENT_PROFILE_CAP_PERCENT;
    case STATUS_LED_PROFILE_FACTORY:
        return STATUS_LED_FULL_BRIGHTNESS_PERCENT;
    case STATUS_LED_PROFILE_STANDARD:
    default:
        return STATUS_LED_STANDARD_PROFILE_CAP_PERCENT;
    }
}

static uint32_t status_led_profile_budget_ma_for(status_led_profile_t profile, bool safety)
{
    if (safety || profile == STATUS_LED_PROFILE_FACTORY) {
        return STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA;
    }
    switch (profile) {
    case STATUS_LED_PROFILE_OFF:
        return 0U;
    case STATUS_LED_PROFILE_LOW:
        return STATUS_LED_LOW_PROFILE_BUDGET_MA;
    case STATUS_LED_PROFILE_AMBIENT:
        return STATUS_LED_AMBIENT_PROFILE_BUDGET_MA;
    case STATUS_LED_PROFILE_STANDARD:
    default:
        return STATUS_LED_STANDARD_PROFILE_BUDGET_MA;
    }
}

static uint32_t status_led_profile_budget_ma_locked(bool safety)
{
    return status_led_profile_budget_ma_for(s_state.profile, safety);
}

static uint8_t status_led_effect_percent_locked(uint8_t desired_percent, bool safety)
{
    if (desired_percent == 0U) {
        return 0U;
    }
    uint32_t scaled = desired_percent;
    if (!safety) {
        uint8_t user_brightness = s_state.brightness_percent;
        scaled = ((uint32_t)desired_percent * user_brightness + 50U) / 100U;
    }
    uint8_t cap = status_led_profile_cap_percent_for(s_state.profile, safety);
    if (scaled > cap) {
        scaled = cap;
    }
    return status_led_clamp_u32_to_u8(scaled);
}

static status_led_rgb_t status_led_token_locked(status_led_rgb_t color, uint8_t desired_percent, bool safety)
{
    return status_led_scale_gamma(color, status_led_effect_percent_locked(desired_percent, safety));
}

static uint8_t status_led_triangle_percent(uint32_t now_ms, uint32_t period_ms, uint8_t min_percent, uint8_t max_percent)
{
    if (period_ms == 0 || max_percent <= min_percent) {
        return max_percent;
    }
    uint32_t phase = now_ms % period_ms;
    uint32_t half = period_ms / 2U;
    uint32_t range = (uint32_t)(max_percent - min_percent);
    if (phase <= half) {
        return (uint8_t)(min_percent + (range * phase) / half);
    }
    return (uint8_t)(min_percent + (range * (period_ms - phase)) / half);
}

static bool status_led_blink_on(uint32_t now_ms, uint32_t on_ms, uint32_t off_ms)
{
    uint32_t period = on_ms + off_ms;
    if (period == 0) {
        return true;
    }
    return (now_ms % period) < on_ms;
}

static bool status_led_double_pulse_on(uint32_t now_ms, uint32_t period_ms)
{
    uint32_t phase = now_ms % period_ms;
    return phase < 120U || (phase >= 240U && phase < 360U);
}

static bool status_led_error_pulse_on_locked(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_state.error_started_ms;
    if (s_state.error_severity == STATUS_LED_ERROR_HARD) {
        return elapsed >= 1200U || status_led_blink_on(elapsed, 120U, 120U);
    }
    return elapsed < 1080U ? status_led_blink_on(elapsed, 180U, 180U) : true;
}

static uint8_t status_led_error_percent_locked(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_state.error_started_ms;
    if (s_state.error_severity == STATUS_LED_ERROR_HARD) {
        return elapsed >= 1200U ? 32U : 80U;
    }
    return elapsed >= 1080U ? 24U : 55U;
}

static esp_err_t status_led_transmit_strip(status_led_strip_t *strip, const status_led_rgb_t *colors)
{
    return status_led_strip_backend_transmit(strip->backend, strip->color_order, colors);
}

static bool status_led_frame_equal(const status_led_frame_t *left, const status_led_frame_t *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void status_led_transmit_frame(const status_led_frame_t *frame)
{
    bool tx_locked = false;
    if (s_tx_mutex != NULL) {
        if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(STATUS_LED_TX_MUTEX_WAIT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "LED transmit skipped: tx mutex timeout");
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_OUTPUT_FAIL, DIAG_SEV_WARN,
                     0, (uint32_t)ESP_ERR_TIMEOUT, 2, 0);
            return;
        }
        tx_locked = true;
    }

    (void)status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_STATUS], frame->status);
    (void)status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EC11], frame->ec11);
    (void)status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_KEY], frame->key);
    (void)status_led_transmit_strip(&s_strips[STATUS_LED_STRIP_EDGE], frame->edge);

    if (tx_locked) {
        xSemaphoreGive(s_tx_mutex);
    }
}

static uint32_t status_led_estimate_current_ma(const status_led_frame_t *frame)
{
    uint32_t channel_sum = 0;
    for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
        channel_sum += frame->status[index].r + frame->status[index].g + frame->status[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        channel_sum += frame->ec11[index].r + frame->ec11[index].g + frame->ec11[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        channel_sum += frame->key[index].r + frame->key[index].g + frame->key[index].b;
    }
    for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
        channel_sum += frame->edge[index].r + frame->edge[index].g + frame->edge[index].b;
    }
    return (channel_sum * 20U + 254U) / 255U;
}

static void status_led_scale_frame_percent(status_led_frame_t *frame, uint8_t percent)
{
    for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
        frame->status[index] = status_led_scale_raw(frame->status[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
        frame->ec11[index] = status_led_scale_raw(frame->ec11[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        frame->key[index] = status_led_scale_raw(frame->key[index], percent);
    }
    for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
        frame->edge[index] = status_led_scale_raw(frame->edge[index], percent);
    }
}

static void status_led_clamp_current_locked(status_led_frame_t *frame, bool safety)
{
    s_state.last_budget_scale_percent = 100U;
    s_state.current_limited_by_budget = false;

    uint32_t estimated_ma = status_led_estimate_current_ma(frame);
    uint32_t budget_ma = status_led_profile_budget_ma_locked(safety);
    if (estimated_ma > budget_ma && estimated_ma > 0 && budget_ma > 0) {
        uint8_t scale_percent = (uint8_t)((budget_ma * 100U) / estimated_ma);
        if (scale_percent == 0) {
            scale_percent = 1;
        }
        status_led_scale_frame_percent(frame, scale_percent);
        s_state.last_budget_scale_percent = scale_percent;
        s_state.current_limited_by_budget = true;
        estimated_ma = status_led_estimate_current_ma(frame);
    } else if (budget_ma == 0 && estimated_ma > 0) {
        memset(frame, 0, sizeof(*frame));
        s_state.last_budget_scale_percent = 0U;
        s_state.current_limited_by_budget = true;
        estimated_ma = 0;
    }
    s_state.last_estimated_current_ma = estimated_ma;
    s_state.last_current_budget_ma = budget_ma;
}

static void status_led_copy_frame_locked(const status_led_frame_t *frame)
{
    memcpy(&s_state.last_frame, frame, sizeof(s_state.last_frame));
}

static void status_led_request_refresh(void)
{
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

static bool status_led_timed_output_active_locked(uint32_t now_ms)
{
    if (s_state.output_disabled || s_state.low_power_disabled) {
        return false;
    }
    if (s_state.test_mode != STATUS_LED_TEST_NONE ||
        s_state.recording_active ||
        s_state.processing_active) {
        return true;
    }
    if (now_ms < s_state.boot_feedback_until_ms ||
        now_ms < s_state.status_window_until_ms ||
        now_ms < s_state.ble_confidence_until_ms ||
        now_ms < s_state.oobe_confidence_until_ms ||
        now_ms < s_state.error_until_ms ||
        now_ms < s_state.ok_until_ms) {
        return true;
    }
    if (s_state.key_pressed_mask != 0U) {
        return true;
    }
    for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        if (now_ms < s_state.key_until_ms[index]) {
            return true;
        }
    }
    if (s_state.external_power_present && s_state.charging && !s_state.full) {
        return true;
    }
    if (!s_state.external_power_present && s_state.battery_valid &&
        s_state.battery_level_percent < 20U) {
        return true;
    }
    if (!s_state.external_power_present &&
        s_state.ble_state != STATUS_LED_BLE_DISCONNECTED) {
        return true;
    }
    return s_state.profile == STATUS_LED_PROFILE_AMBIENT;
}

static uint32_t status_led_refresh_delay_ms_locked(uint32_t now_ms)
{
    return status_led_timed_output_active_locked(now_ms)
        ? STATUS_LED_REFRESH_MS
        : STATUS_LED_IDLE_REFRESH_MS;
}

static void status_led_render_test_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    status_led_rgb_t color = {0};
    uint32_t elapsed = now_ms - s_state.test_started_ms;
    if (s_state.test_mode == STATUS_LED_TEST_RGBW) {
        switch ((elapsed / 1000U) % 4U) {
        case 0: color = status_led_token_locked(status_led_rgb(255, 0, 0), 100U, true); break;
        case 1: color = status_led_token_locked(status_led_rgb(0, 255, 0), 100U, true); break;
        case 2: color = status_led_token_locked(status_led_rgb(0, 0, 255), 100U, true); break;
        default: color = status_led_token_locked(status_led_rgb(255, 255, 255), 100U, true); break;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            for (size_t index = 0; index < STATUS_LED_STATUS_COUNT; ++index) {
                frame->status[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            for (size_t index = 0; index < STATUS_LED_EC11_COUNT; ++index) {
                frame->ec11[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            for (size_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
                frame->key[index] = color;
            }
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
                frame->edge[index] = color;
            }
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_MAP) {
        color = status_led_token_locked(status_led_rgb(255, 255, 255), 80U, true);
        uint32_t step = elapsed / 600U;
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            frame->status[step % STATUS_LED_STATUS_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            frame->ec11[step % STATUS_LED_EC11_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            frame->key[step % STATUS_LED_KEY_COUNT] = color;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            frame->edge[step % STATUS_LED_EDGE_COUNT] = color;
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_CHASE) {
        const uint32_t step_ms = s_state.test_step_ms > 0U
            ? (uint32_t)s_state.test_step_ms
            : STATUS_LED_CHASE_DEFAULT_STEP_MS;
        uint32_t total = 0;
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            total += STATUS_LED_STATUS_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            total += STATUS_LED_EC11_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            total += STATUS_LED_KEY_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE)) {
            total += STATUS_LED_EDGE_COUNT;
        }
        if (total == 0U) {
            return;
        }

        uint32_t step = elapsed / step_ms;
        uint32_t cursor = step % total;
        switch ((step / total) % 4U) {
        case 0: color = status_led_token_locked(status_led_rgb(255, 0, 0), 100U, true); break;
        case 1: color = status_led_token_locked(status_led_rgb(0, 255, 0), 100U, true); break;
        case 2: color = status_led_token_locked(status_led_rgb(0, 0, 255), 100U, true); break;
        default: color = status_led_token_locked(status_led_rgb(255, 255, 255), 100U, true); break;
        }

        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_STATUS)) {
            if (cursor < STATUS_LED_STATUS_COUNT) {
                frame->status[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_STATUS_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EC11)) {
            if (cursor < STATUS_LED_EC11_COUNT) {
                frame->ec11[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_EC11_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_KEY)) {
            if (cursor < STATUS_LED_KEY_COUNT) {
                frame->key[cursor] = color;
                return;
            }
            cursor -= STATUS_LED_KEY_COUNT;
        }
        if (s_state.test_strip_mask & (1U << STATUS_LED_STRIP_EDGE) &&
            cursor < STATUS_LED_EDGE_COUNT) {
            frame->edge[cursor] = color;
        }
        return;
    }

    if (s_state.test_mode == STATUS_LED_TEST_PIXEL) {
        color = status_led_token_locked(s_state.test_pixel_color, s_state.test_pixel_percent, true);
        switch (s_state.test_pixel_strip) {
        case STATUS_LED_STRIP_STATUS:
            if (s_state.test_pixel_index < STATUS_LED_STATUS_COUNT) {
                frame->status[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_KEY:
            if (s_state.test_pixel_index < STATUS_LED_KEY_COUNT) {
                frame->key[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_EC11:
            if (s_state.test_pixel_index < STATUS_LED_EC11_COUNT) {
                frame->ec11[s_state.test_pixel_index] = color;
            }
            break;
        case STATUS_LED_STRIP_EDGE:
            if (s_state.test_pixel_index < STATUS_LED_EDGE_COUNT) {
                frame->edge[s_state.test_pixel_index] = color;
            }
            break;
        default:
            break;
        }
    }
}

static void status_led_render_power_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    const bool status_window = now_ms < s_state.status_window_until_ms;
    bool safety = false;
    uint8_t percent = 0;
    status_led_rgb_t color = {0};

    if (s_state.external_power_present) {
        if (s_state.full) {
            percent = status_window
                ? STATUS_LED_FULL_STATUS_STEADY_PERCENT
                : STATUS_LED_FULL_STEADY_PERCENT;
        } else {
            percent = status_led_triangle_percent(
                now_ms,
                STATUS_LED_CHARGING_BREATH_PERIOD_MS,
                STATUS_LED_CHARGING_BREATH_MIN_PERCENT,
                STATUS_LED_CHARGING_BREATH_MAX_PERCENT);
        }
        color = status_led_token_locked(status_led_rgb(255, 255, 255), percent, false);
    } else if (s_state.battery_valid) {
        if (s_state.battery_level_percent < 10U) {
            safety = true;
            if (status_led_double_pulse_on(now_ms, 4000U)) {
                color = status_led_token_locked(status_led_rgb(255, 0, 0), 100U, true);
            }
        } else if (s_state.battery_level_percent < 20U) {
            safety = true;
            if (status_led_blink_on(now_ms, 500U, 2500U)) {
                color = status_led_token_locked(status_led_rgb(255, 0, 0), 80U, true);
            }
        } else {
            percent = status_window ? 46U : 0U;
            if (s_state.profile == STATUS_LED_PROFILE_LOW || s_state.profile == STATUS_LED_PROFILE_OFF) {
                percent = status_window ? 30U : 0U;
            }
            if (s_state.battery_level_percent >= 60U) {
                color = status_led_token_locked(status_led_rgb(0, 255, 0), percent, false);
            } else {
                color = status_led_token_locked(status_led_rgb(255, 140, 0), percent, false);
            }
        }
    }

    status_led_set_max(&frame->status[STATUS_LED_SEM_PWR], color);
    if (now_ms < s_state.boot_feedback_until_ms) {
        status_led_set_max(
            &frame->status[STATUS_LED_SEM_PWR],
            status_led_boot_power_color_locked());
        safety = true;
    }
    *ret_safety = *ret_safety || safety;
}

static void status_led_render_ble_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    status_led_rgb_t ble_blue = status_led_rgb(0, 0, 255);
    status_led_rgb_t color = {0};
    const bool confidence = now_ms < s_state.ble_confidence_until_ms ||
                            now_ms < s_state.oobe_confidence_until_ms;
    const bool status_window = now_ms < s_state.status_window_until_ms;
    const bool battery_idle = !s_state.external_power_present && !confidence && !status_window;

    switch (s_state.ble_state) {
    case STATUS_LED_BLE_PAIRING:
        if (battery_idle && status_led_blink_on(
                                now_ms,
                                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS,
                                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS)) {
            color = status_led_token_locked(
                ble_blue,
                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT,
                false);
        } else if (!battery_idle && status_led_blink_on(now_ms, 420U, 680U)) {
            color = status_led_token_locked(ble_blue, 65U, false);
        }
        break;
    case STATUS_LED_BLE_RECONNECTING:
        if (battery_idle && status_led_blink_on(
                                now_ms - s_state.last_transition_ms,
                                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS,
                                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS)) {
            color = status_led_token_locked(
                ble_blue,
                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT,
                false);
        } else if (!battery_idle &&
                   status_led_double_pulse_on(now_ms - s_state.last_transition_ms, 2000U)) {
            color = status_led_token_locked(ble_blue, 58U, false);
        }
        break;
    case STATUS_LED_BLE_CONNECTED:
        if (confidence || status_window) {
            color = status_led_token_locked(ble_blue, 48U, false);
        } else if (battery_idle && status_led_blink_on(
                                   now_ms - s_state.last_transition_ms,
                                   STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_ON_MS,
                                   STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_OFF_MS)) {
            color = status_led_token_locked(
                ble_blue,
                STATUS_LED_BATTERY_IDLE_BLE_HEARTBEAT_PERCENT,
                false);
        } else if (!battery_idle && s_state.profile == STATUS_LED_PROFILE_STANDARD) {
            color = status_led_token_locked(ble_blue, 30U, false);
        } else if (!battery_idle && s_state.profile == STATUS_LED_PROFILE_AMBIENT) {
            color = status_led_token_locked(ble_blue, 38U, false);
        }
        break;
    case STATUS_LED_BLE_DISCONNECTED:
    default:
        break;
    }

    status_led_set_max(&frame->status[STATUS_LED_SEM_BLE], color);
}

static void status_led_render_recording_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (!s_state.recording_active) {
        return;
    }
    if (s_state.rec_source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
        return;
    }
    uint8_t breath = status_led_triangle_percent(
        now_ms,
        STATUS_LED_RECORDING_BREATH_PERIOD_MS,
        STATUS_LED_RECORDING_BREATH_MIN_PERCENT,
        STATUS_LED_RECORDING_BREATH_MAX_PERCENT);
    uint8_t level = s_state.recording_level_percent;
    if (s_state.recording_level_updated_ms == 0U ||
        now_ms - s_state.recording_level_updated_ms > STATUS_LED_RECORDING_LEVEL_STALE_MS) {
        level = 0U;
    }
    uint8_t level_percent = 0U;
    if (level > 0U) {
        uint32_t boosted_level = (uint32_t)level * STATUS_LED_RECORDING_LEVEL_BOOST_MULTIPLIER;
        if (boosted_level > 100U) {
            boosted_level = 100U;
        }
        level_percent = STATUS_LED_RECORDING_LEVEL_FLOOR_PERCENT +
            (uint8_t)((boosted_level * STATUS_LED_RECORDING_LEVEL_RANGE_PERCENT) / 100U);
    }
    uint8_t percent = level_percent > breath ? level_percent : breath;
    status_led_rgb_t rec = status_led_token_locked(status_led_rec_gold(), percent, false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_REC], rec);
    (void)ret_safety;
}

static void status_led_render_processing_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (!s_state.processing_active) {
        return;
    }
    uint32_t elapsed = now_ms - s_state.processing_started_ms;
    uint8_t max_percent = elapsed > 10000U ? 42U : 70U;
    uint8_t breath = status_led_triangle_percent(now_ms, 3200U, 24U, max_percent);
    status_led_rgb_t ai = status_led_token_locked(status_led_rgb(160, 0, 255), breath, false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_AI], ai);

    if (s_state.profile == STATUS_LED_PROFILE_AMBIENT) {
        uint32_t dot = (now_ms / 350U) % STATUS_LED_EDGE_COUNT;
        status_led_rgb_t edge = status_led_token_locked(status_led_rgb(160, 0, 255), elapsed > 10000U ? 28U : 40U, false);
        frame->edge[dot] = edge;
        frame->edge[(dot + 1U) % STATUS_LED_EDGE_COUNT] = status_led_scale_raw(edge, 35U);
    }
}

static void status_led_render_ok_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (now_ms >= s_state.ok_until_ms || s_state.ok_started_ms == 0) {
        return;
    }
    uint32_t elapsed = now_ms - s_state.ok_started_ms;
    uint8_t percent = 0;
    if (elapsed <= STATUS_LED_OK_PEAK_MS) {
        percent = 85U;
    } else if (elapsed < STATUS_LED_OK_TOTAL_MS) {
        uint32_t remaining = STATUS_LED_OK_TOTAL_MS - elapsed;
        percent = (uint8_t)((85U * remaining) / (STATUS_LED_OK_TOTAL_MS - STATUS_LED_OK_PEAK_MS));
    }
    status_led_rgb_t ok = status_led_token_locked(status_led_rgb(0, 255, 0), percent, false);
    status_led_set_max(&frame->status[STATUS_LED_SEM_OK], ok);
}

static status_led_semantic_t status_led_error_source_semantic(status_led_error_domain_t domain)
{
    switch (domain) {
    case STATUS_LED_ERROR_DOMAIN_BLE: return STATUS_LED_SEM_BLE;
    case STATUS_LED_ERROR_DOMAIN_REC: return STATUS_LED_SEM_REC;
    case STATUS_LED_ERROR_DOMAIN_AI:
    case STATUS_LED_ERROR_DOMAIN_OTA: return STATUS_LED_SEM_AI;
    case STATUS_LED_ERROR_DOMAIN_POWER: return STATUS_LED_SEM_PWR;
    case STATUS_LED_ERROR_DOMAIN_SYSTEM:
    case STATUS_LED_ERROR_DOMAIN_NONE:
    default: return STATUS_LED_SEM_PWR;
    }
}

static status_led_rgb_t status_led_error_source_color(status_led_error_domain_t domain)
{
    switch (domain) {
    case STATUS_LED_ERROR_DOMAIN_BLE: return status_led_rgb(0, 0, 255);
    case STATUS_LED_ERROR_DOMAIN_REC: return status_led_rec_gold();
    case STATUS_LED_ERROR_DOMAIN_AI:
    case STATUS_LED_ERROR_DOMAIN_OTA: return status_led_rgb(160, 0, 255);
    case STATUS_LED_ERROR_DOMAIN_POWER:
    case STATUS_LED_ERROR_DOMAIN_SYSTEM:
    case STATUS_LED_ERROR_DOMAIN_NONE:
    default: return status_led_rgb(255, 255, 255);
    }
}

static void status_led_render_error_locked(status_led_frame_t *frame, uint32_t now_ms, bool *ret_safety)
{
    if (s_state.error_domain == STATUS_LED_ERROR_DOMAIN_NONE || now_ms >= s_state.error_until_ms) {
        return;
    }
    if (!status_led_error_pulse_on_locked(now_ms)) {
        return;
    }

    bool hard = s_state.error_severity == STATUS_LED_ERROR_HARD;
    uint8_t percent = status_led_error_percent_locked(now_ms);
    status_led_rgb_t warn_color = hard
        ? status_led_rgb(255, 0, 0)
        : status_led_rgb(255, 140, 0);
    status_led_rgb_t warn = status_led_token_locked(warn_color, percent, true);
    status_led_rgb_t source = status_led_token_locked(
        status_led_error_source_color(s_state.error_domain),
        percent > 4U ? (uint8_t)(percent - 3U) : percent,
        true);

    status_led_set_max(&frame->status[STATUS_LED_SEM_WARN], warn);
    status_led_set_max(
        &frame->status[status_led_error_source_semantic(s_state.error_domain)],
        source);
    *ret_safety = true;
}

static void status_led_render_keys_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    for (uint8_t index = 0; index < STATUS_LED_KEY_COUNT; ++index) {
        bool pressed = (s_state.key_pressed_mask & (1U << index)) != 0;
        if (!pressed && now_ms >= s_state.key_until_ms[index]) {
            continue;
        }
        status_led_rgb_t color = status_led_token_locked(status_led_rgb(255, 255, 255), pressed ? 72U : 28U, false);
        status_led_set_max(&frame->key[index], color);
    }
}

static void status_led_render_edge_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    if (s_state.error_domain != STATUS_LED_ERROR_DOMAIN_NONE && now_ms < s_state.error_until_ms) {
        return;
    }
    if (s_state.battery_valid && s_state.battery_level_percent < 20U) {
        return;
    }
    if (s_state.profile != STATUS_LED_PROFILE_AMBIENT) {
        return;
    }
    if (s_state.ble_state == STATUS_LED_BLE_PAIRING) {
        uint32_t dot = (now_ms / 400U) % STATUS_LED_EDGE_COUNT;
        frame->edge[dot] = status_led_token_locked(status_led_rgb(0, 0, 255), 40U, false);
        return;
    }
    if (s_state.ble_state == STATUS_LED_BLE_RECONNECTING) {
        if (status_led_double_pulse_on(now_ms, 2000U)) {
            frame->edge[0] = status_led_token_locked(status_led_rgb(0, 0, 255), 34U, false);
            frame->edge[3] = status_led_token_locked(status_led_rgb(0, 0, 255), 34U, false);
        }
        return;
    }
    if (s_state.profile == STATUS_LED_PROFILE_AMBIENT) {
        uint8_t percent = status_led_triangle_percent(now_ms, 4200U, 22U, 42U);
        status_led_rgb_t color = status_led_token_locked(status_led_rgb(0, 0, 255), percent, false);
        for (size_t index = 0; index < STATUS_LED_EDGE_COUNT; ++index) {
            status_led_set_max(&frame->edge[index], color);
        }
    }
}

static void status_led_render_frame_locked(status_led_frame_t *frame, uint32_t now_ms)
{
    memset(frame, 0, sizeof(*frame));
    bool safety = false;

    if (s_state.output_disabled || s_state.low_power_disabled) {
        s_state.last_estimated_current_ma = 0;
        s_state.last_current_budget_ma = 0;
        s_state.last_budget_scale_percent = 0U;
        s_state.current_limited_by_budget = false;
        return;
    }

    if (s_state.test_mode != STATUS_LED_TEST_NONE) {
        status_led_render_test_locked(frame, now_ms);
        status_led_clamp_current_locked(frame, true);
        return;
    }

    if (now_ms >= s_state.error_until_ms) {
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    }

    status_led_render_power_locked(frame, now_ms, &safety);
    status_led_render_ble_locked(frame, now_ms);
    status_led_render_recording_locked(frame, now_ms, &safety);
    status_led_render_processing_locked(frame, now_ms);
    status_led_render_keys_locked(frame, now_ms);
    status_led_render_edge_locked(frame, now_ms);
    status_led_render_ok_locked(frame, now_ms);
    status_led_render_error_locked(frame, now_ms, &safety);

    status_led_clamp_current_locked(frame, safety);
}

static uint32_t status_led_refresh_once(void)
{
    status_led_frame_t frame;
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    uint32_t delay_ms = STATUS_LED_IDLE_REFRESH_MS;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return STATUS_LED_REFRESH_MS;
    }
    status_led_render_frame_locked(&frame, now_ms);
    changed = !status_led_frame_equal(&s_state.last_frame, &frame);
    status_led_copy_frame_locked(&frame);
    delay_ms = status_led_refresh_delay_ms_locked(now_ms);
    xSemaphoreGive(s_mutex);

    if (changed) {
        status_led_transmit_frame(&frame);
    }
    return delay_ms;
}

static void status_led_set_last_reason_locked(const char *reason)
{
    snprintf(s_state.last_reason, sizeof(s_state.last_reason), "%s", reason != NULL ? reason : "none");
}

static void status_led_resume_output_locked(void)
{
    if (!s_state.low_power_disabled) {
        s_state.output_disabled = false;
    }
}

static void status_led_force_manual_off(void)
{
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.output_disabled = true;
        s_state.low_power_disabled = false;
        s_state.test_mode = STATUS_LED_TEST_NONE;
        s_state.key_pressed_mask = 0;
        memset(s_state.key_until_ms, 0, sizeof(s_state.key_until_ms));
        s_state.ok_until_ms = 0;
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        s_state.error_until_ms = 0;
        s_state.status_window_until_ms = 0;
        s_state.ble_confidence_until_ms = 0;
        s_state.oobe_confidence_until_ms = 0;
        s_state.last_transition_ms = status_led_now_ms();
        status_led_set_last_reason_locked("manual_off");
        xSemaphoreGive(s_mutex);
    }
    status_led_force_all_off();
}

static void status_led_poll_power_inputs(void)
{
    uint32_t now_ms = status_led_now_ms();
    bool should_poll = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool preview_window_active = now_ms < s_state.status_window_until_ms &&
                                     strcmp(s_state.last_reason, "preview") == 0;
        should_poll = !preview_window_active &&
                      (s_state.last_power_poll_ms == 0 ||
                       now_ms - s_state.last_power_poll_ms >= STATUS_LED_POWER_POLL_MS);
        if (should_poll) {
            s_state.last_power_poll_ms = now_ms;
        }
        xSemaphoreGive(s_mutex);
    }
    if (!should_poll) {
        return;
    }

    battery_monitor_status_t battery = {0};
    esp_err_t battery_ret = battery_monitor_read(&battery);
    bool battery_valid = battery_ret == ESP_OK && battery.valid;
    uint32_t battery_mv = battery_valid ? battery.voltage_mv : 0;
    uint8_t battery_level = battery_valid ? battery.level_percent : 0xFF;
    bool usb_power_present = BOARD_PINS_USB_DET_IO != GPIO_NUM_NC &&
                             gpio_get_level(BOARD_PINS_USB_DET_IO) > 0;
    bool raw_charging = usb_power_present &&
                        BOARD_PINS_BAT_CHG_IO != GPIO_NUM_NC &&
                        gpio_get_level(BOARD_PINS_BAT_CHG_IO) == 0;
    bool raw_full = usb_power_present &&
                    BOARD_PINS_BAT_STD_IO != GPIO_NUM_NC &&
                    gpio_get_level(BOARD_PINS_BAT_STD_IO) == 0;
    bool external_power_present = usb_power_present;
    uint8_t active_brightness = device_settings_get_active_brightness_percent(external_power_present);

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (!usb_power_present) {
            s_state.charge_full_latched = false;
            s_state.charge_full_candidate_since_ms = 0;
        } else if (!s_state.charge_full_latched) {
            bool battery_allows_full = !battery_valid ||
                                       battery_mv >= STATUS_LED_CHARGE_FULL_MIN_MV ||
                                       battery_level >= STATUS_LED_CHARGE_FULL_MIN_PERCENT;
            bool full_candidate = raw_full && !raw_charging && battery_allows_full;
            if (full_candidate) {
                if (s_state.charge_full_candidate_since_ms == 0) {
                    s_state.charge_full_candidate_since_ms = now_ms;
                } else if (now_ms - s_state.charge_full_candidate_since_ms >=
                           STATUS_LED_CHARGE_FULL_DEBOUNCE_MS) {
                    s_state.charge_full_latched = true;
                }
            } else {
                s_state.charge_full_candidate_since_ms = 0;
            }
        }
        bool full = usb_power_present && s_state.charge_full_latched;
        bool charging = usb_power_present && !full;
        bool battery_band_changed =
            s_state.battery_valid != battery_valid ||
            (battery_valid && (s_state.battery_level_percent / 10U) != (battery_level / 10U));
        bool power_visual_changed =
            s_state.external_power_present != external_power_present ||
            s_state.charging != charging ||
            s_state.full != full ||
            s_state.brightness_percent != active_brightness ||
            (!external_power_present && battery_band_changed);
        s_state.battery_valid = battery_valid;
        s_state.battery_mv = battery_mv;
        s_state.battery_level_percent = battery_level;
        s_state.external_power_present = external_power_present;
        s_state.charging = charging;
        s_state.full = full;
        s_state.raw_charging = raw_charging;
        s_state.raw_full = raw_full;
        s_state.brightness_percent = active_brightness;
        if (power_visual_changed) {
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
            s_state.last_transition_ms = now_ms;
            status_led_set_last_reason_locked("power_change");
        }
        xSemaphoreGive(s_mutex);
    }
}

void status_led_apply_device_settings(void)
{
    bool changed = false;
    if (s_mutex == NULL) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        uint8_t active_brightness =
            device_settings_get_active_brightness_percent(s_state.external_power_present);
        s_state.brightness_percent = active_brightness;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked("device_settings");
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

static void status_led_task(void *parameter)
{
    (void)parameter;
    while (1) {
        status_led_poll_power_inputs();
        uint32_t delay_ms = status_led_refresh_once();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
}

static esp_err_t status_led_init_strip_backend(status_led_strip_t *strip)
{
    status_led_strip_backend_config_t config = {
        .name = strip->name,
        .gpio = strip->gpio,
        .led_count = strip->led_count,
        .color_order = strip->color_order,
    };
    return status_led_strip_backend_new(&config, &strip->backend);
}

static void status_led_configure_power_inputs(void)
{
    uint64_t charge_mask = 0;
    if (BOARD_PINS_BAT_CHG_IO != GPIO_NUM_NC) {
        charge_mask |= 1ULL << (uint32_t)BOARD_PINS_BAT_CHG_IO;
    }
    if (BOARD_PINS_BAT_STD_IO != GPIO_NUM_NC) {
        charge_mask |= 1ULL << (uint32_t)BOARD_PINS_BAT_STD_IO;
    }
    if (charge_mask != 0) {
        gpio_config_t charge_config = {
            .pin_bit_mask = charge_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&charge_config);
    }

    if (BOARD_PINS_USB_DET_IO != GPIO_NUM_NC) {
        gpio_config_t usb_config = {
            .pin_bit_mask = 1ULL << (uint32_t)BOARD_PINS_USB_DET_IO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&usb_config);
    }
}

static status_led_profile_t status_led_profile_from_u8(uint8_t value)
{
    if (value <= STATUS_LED_PROFILE_FACTORY) {
        return (status_led_profile_t)value;
    }
    return STATUS_LED_PROFILE_STANDARD;
}

static uint8_t status_led_brightness_from_u8(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static void status_led_load_persistent_config(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(STATUS_LED_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        return;
    }
    uint8_t profile = (uint8_t)STATUS_LED_PROFILE_STANDARD;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_PROFILE_KEY, &profile) == ESP_OK) {
        s_state.profile = status_led_profile_from_u8(profile);
    }
    uint8_t brightness = 100U;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_BRIGHTNESS_KEY, &brightness) == ESP_OK) {
        s_state.brightness_percent = status_led_brightness_from_u8(brightness);
    }
    uint8_t order = 0;
    if (nvs_get_u8(nvs, STATUS_LED_NVS_STATUS_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_STATUS].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_EC11_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_EC11].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_KEY_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_KEY].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    if (nvs_get_u8(nvs, STATUS_LED_NVS_EDGE_ORDER_KEY, &order) == ESP_OK) {
        s_strips[STATUS_LED_STRIP_EDGE].color_order = order == STATUS_LED_COLOR_ORDER_RGB
            ? STATUS_LED_COLOR_ORDER_RGB
            : STATUS_LED_COLOR_ORDER_GRB;
    }
    nvs_close(nvs);
}

static void status_led_save_profile(status_led_profile_t profile)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(STATUS_LED_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "profile persist skipped: %s", esp_err_to_name(ret));
        return;
    }
    ret = nvs_set_u8(nvs, STATUS_LED_NVS_PROFILE_KEY, (uint8_t)profile);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "profile persist failed: %s", esp_err_to_name(ret));
    }
}

static void status_led_save_brightness(uint8_t brightness_percent)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(STATUS_LED_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "brightness persist skipped: %s", esp_err_to_name(ret));
        return;
    }
    ret = nvs_set_u8(nvs, STATUS_LED_NVS_BRIGHTNESS_KEY, brightness_percent);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "brightness persist failed: %s", esp_err_to_name(ret));
    }
}

static void status_led_force_all_off(void)
{
    status_led_frame_t frame = {0};
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_copy_frame_locked(&frame);
        xSemaphoreGive(s_mutex);
    }
    status_led_transmit_frame(&frame);
}

static status_led_rgb_t status_led_boot_power_color_locked(void)
{
    return status_led_token_locked(status_led_rgb(255, 255, 255), 48U, true);
}

static bool status_led_reason_is_boot_feedback(const char *reason)
{
    return reason != NULL &&
           (strcmp(reason, "boot") == 0 || strcmp(reason, "booting") == 0);
}

static void status_led_mark_boot_feedback_locked(uint32_t now_ms)
{
    uint32_t until_ms = now_ms + STATUS_LED_BOOT_ACK_MS;
    if (s_state.boot_feedback_until_ms < until_ms) {
        s_state.boot_feedback_until_ms = until_ms;
    }
}

static void status_led_force_boot_feedback(void)
{
    status_led_frame_t frame = {0};
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_mark_boot_feedback_locked(now_ms);
        frame.status[STATUS_LED_SEM_PWR] = status_led_boot_power_color_locked();
        status_led_copy_frame_locked(&frame);
        xSemaphoreGive(s_mutex);
        status_led_transmit_frame(&frame);
    }
}

esp_err_t status_led_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_state.initialized) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.profile = STATUS_LED_PROFILE_STANDARD;
    s_state.brightness_percent = 100U;
    s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
    s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
    s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
    s_state.vdd_led_enable_assumed = true;
    uint32_t now_ms = status_led_now_ms();
    s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
    s_state.boot_feedback_until_ms = now_ms + STATUS_LED_BOOT_ACK_MS;
    status_led_set_last_reason_locked("boot");

    status_led_load_persistent_config();
    status_led_configure_power_inputs();

    esp_err_t final_ret = ESP_OK;
    for (size_t index = 0; index < STATUS_LED_STRIP_COUNT; ++index) {
        esp_err_t ret = status_led_init_strip_backend(&s_strips[index]);
        if (ret != ESP_OK && final_ret == ESP_OK) {
            final_ret = ret;
        }
    }

    s_state.initialized = true;
    ESP_LOGI(
        TAG,
        "status LED init: profile=%s status_gpio=%d ec11_gpio=%d key_gpio=%d edge_gpio=%d key_pin_contract=GPIO13 ec11_pin_contract=GPIO5 vdd_led_enable=always_on_assumed",
        status_led_profile_name(s_state.profile),
        (int)BOARD_PINS_RGB_STATUS_IO,
        (int)BOARD_PINS_RGB_EC11_IO,
        (int)BOARD_PINS_RGB_KEY_IO,
        (int)BOARD_PINS_RGB_EDGE_IO);
    diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
             (uint32_t)s_state.profile, (uint32_t)BOARD_PINS_RGB_STATUS_IO,
             (uint32_t)BOARD_PINS_RGB_KEY_IO, (uint32_t)BOARD_PINS_RGB_EDGE_IO);
    status_led_force_all_off();
    return final_ret;
}

esp_err_t status_led_start(void)
{
    esp_err_t ret = status_led_init();
    if (ret != ESP_OK) {
        return ret;
    }
    if (s_state.started) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_load_persistent_config();
        xSemaphoreGive(s_mutex);
    }
    status_led_force_boot_feedback();

    BaseType_t ok = xTaskCreate(
        status_led_task,
        "status_led_task",
        STATUS_LED_TASK_STACK_BYTES,
        NULL,
        3,
        &s_task_handle);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_state.started = true;
    ESP_LOGI(TAG,
             "status LED task started: refresh_ms=%u idle_refresh_ms=%u backend=rmt_ws2812_800khz",
             STATUS_LED_REFRESH_MS,
             STATUS_LED_IDLE_REFRESH_MS);
    return ESP_OK;
}

void status_led_show_status_window(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        s_state.output_disabled = false;
        s_state.low_power_disabled = false;
        if (status_led_reason_is_boot_feedback(reason)) {
            status_led_mark_boot_feedback_locked(now_ms);
        }
        status_led_set_last_reason_locked(reason);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_ble_state(status_led_ble_state_t state, bool confidence_window)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        changed = s_state.ble_state != state;
        s_state.ble_state = state;
        if (changed) {
            s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        }
        if (changed && state == STATUS_LED_BLE_CONNECTED && confidence_window) {
            s_state.ble_confidence_until_ms = now_ms + STATUS_LED_BLE_CONFIDENCE_MS;
            if (!s_state.ever_connected) {
                s_state.oobe_confidence_until_ms = now_ms + STATUS_LED_OOBE_CONFIDENCE_MS;
            }
            s_state.ever_connected = true;
        }
        if (changed) {
            s_state.last_transition_ms = now_ms;
            status_led_set_last_reason_locked("ble_state");
            diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                     1, (uint32_t)state, confidence_window ? 1U : 0U, 0);
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_recording(bool active, status_led_rec_source_t source)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.recording_active = active && source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE;
        s_state.rec_source = source;
        s_state.recording_level_percent = 0U;
        s_state.recording_level_updated_ms = active ? now_ms : 0U;
        s_state.last_transition_ms = now_ms;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        status_led_set_last_reason_locked(active ? "recording_start" : "recording_stop");
        if (active && source == STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
            s_state.error_domain = STATUS_LED_ERROR_DOMAIN_REC;
            s_state.error_severity = STATUS_LED_ERROR_RETRYABLE;
            s_state.error_started_ms = now_ms;
            s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
        }
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 2, active ? 1U : 0U, (uint32_t)source, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_recording_level(uint8_t level_percent)
{
    if (level_percent > 100U) {
        level_percent = 100U;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_state.recording_active && s_state.rec_source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE) {
            uint8_t current = s_state.recording_level_percent;
            if (level_percent > current) {
                current = (uint8_t)((current + (level_percent * 3U)) / 4U);
            } else {
                current = (uint8_t)(((uint32_t)current * 7U + level_percent) / 8U);
            }
            s_state.recording_level_percent = current;
            s_state.recording_level_updated_ms = now_ms;
            changed = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_processing(bool active, const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.processing_active = active;
        if (active) {
            s_state.processing_started_ms = now_ms;
        }
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : (active ? "processing_start" : "processing_stop"));
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO,
                 3, active ? 1U : 0U, now_ms - s_state.processing_started_ms, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_success(const char *reason)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "success");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_STATE, DIAG_SEV_INFO, 4, 1, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_notify_key_event(uint8_t key_index, bool pressed)
{
    if (key_index >= STATUS_LED_KEY_COUNT) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        if (pressed) {
            s_state.key_pressed_mask |= 1U << key_index;
        } else {
            s_state.key_pressed_mask &= ~(1U << key_index);
        }
        s_state.key_until_ms[key_index] = now_ms + STATUS_LED_KEY_FEEDBACK_MS;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(pressed ? "key_press" : "key_release");
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_error(
    status_led_error_domain_t domain,
    status_led_error_severity_t severity,
    const char *reason)
{
    if (domain == STATUS_LED_ERROR_DOMAIN_NONE) {
        return;
    }
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        status_led_resume_output_locked();
        s_state.error_domain = domain;
        s_state.error_severity = severity;
        s_state.error_started_ms = now_ms;
        s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(reason != NULL ? reason : "error");
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_ERROR, DIAG_SEV_WARN,
                 (uint32_t)domain, (uint32_t)severity, 0, 0);
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_clear_error(status_led_error_domain_t domain)
{
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (domain == STATUS_LED_ERROR_DOMAIN_NONE || s_state.error_domain == domain) {
            s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
            s_state.error_until_ms = 0;
            changed = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_set_low_power_disabled(bool disabled)
{
    uint32_t now_ms = status_led_now_ms();
    bool changed = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        changed = s_state.low_power_disabled != disabled ||
                  (s_state.output_disabled && !disabled);
        s_state.low_power_disabled = disabled;
        if (!disabled) {
            s_state.output_disabled = false;
        }
        s_state.last_transition_ms = now_ms;
        status_led_set_last_reason_locked(disabled ? "low_power_off" : "low_power_resume");
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
}

void status_led_prepare_sleep(void)
{
    bool changed = false;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.low_power_disabled = true;
        s_state.output_disabled = true;
        s_state.test_mode = STATUS_LED_TEST_NONE;
        s_state.last_transition_ms = status_led_now_ms();
        status_led_set_last_reason_locked("prepare_sleep");
        changed = true;
        xSemaphoreGive(s_mutex);
    }
    if (changed) {
        status_led_request_refresh();
    }
    status_led_force_all_off();
}

static const char *status_led_strip_prefix(const char *line)
{
    if (line == NULL) {
        return NULL;
    }
    if (*line == '~') {
        line++;
    }
    size_t prefix_len = strlen(STATUS_LED_USB_PREFIX);
    if (strncmp(line, STATUS_LED_USB_PREFIX, prefix_len) != 0) {
        return NULL;
    }
    return line + prefix_len;
}

static uint8_t status_led_parse_single_strip_mask(const char *text)
{
    if (text == NULL || *text == '\0' || strcasecmp(text, "all") == 0) {
        return (1U << STATUS_LED_STRIP_STATUS) |
               (1U << STATUS_LED_STRIP_EC11) |
               (1U << STATUS_LED_STRIP_KEY) |
               (1U << STATUS_LED_STRIP_EDGE);
    }
    if (strcasecmp(text, "status") == 0) {
        return 1U << STATUS_LED_STRIP_STATUS;
    }
    if (strcasecmp(text, "ec11") == 0 ||
        strcasecmp(text, "knob") == 0 ||
        strcasecmp(text, "ring") == 0) {
        return 1U << STATUS_LED_STRIP_EC11;
    }
    if (strcasecmp(text, "key") == 0) {
        return 1U << STATUS_LED_STRIP_KEY;
    }
    if (strcasecmp(text, "edge") == 0) {
        return 1U << STATUS_LED_STRIP_EDGE;
    }
    return 0;
}

static uint8_t status_led_parse_strip_mask(const char *text)
{
    if (text == NULL || *text == '\0') {
        return status_led_parse_single_strip_mask(text);
    }

    char copy[64] = {0};
    snprintf(copy, sizeof(copy), "%s", text);
    uint8_t mask = 0;
    char *save = NULL;
    char *token = strtok_r(copy, ",+| ", &save);
    while (token != NULL) {
        uint8_t part = status_led_parse_single_strip_mask(token);
        if (part == 0) {
            return 0;
        }
        mask |= part;
        token = strtok_r(NULL, ",+| ", &save);
    }
    return mask;
}

static bool status_led_parse_profile(const char *text, status_led_profile_t *profile)
{
    if (strcasecmp(text, "off") == 0) {
        *profile = STATUS_LED_PROFILE_OFF;
    } else if (strcasecmp(text, "low") == 0) {
        *profile = STATUS_LED_PROFILE_LOW;
    } else if (strcasecmp(text, "standard") == 0) {
        *profile = STATUS_LED_PROFILE_STANDARD;
    } else if (strcasecmp(text, "ambient") == 0) {
        *profile = STATUS_LED_PROFILE_AMBIENT;
    } else if (strcasecmp(text, "factory") == 0) {
        *profile = STATUS_LED_PROFILE_FACTORY;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_domain(const char *text, status_led_error_domain_t *domain)
{
    if (strcasecmp(text, "ble") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_BLE;
    } else if (strcasecmp(text, "rec") == 0 || strcasecmp(text, "recording") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_REC;
    } else if (strcasecmp(text, "ai") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_AI;
    } else if (strcasecmp(text, "ota") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_OTA;
    } else if (strcasecmp(text, "power") == 0 || strcasecmp(text, "pwr") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_POWER;
    } else if (strcasecmp(text, "system") == 0) {
        *domain = STATUS_LED_ERROR_DOMAIN_SYSTEM;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_severity(const char *text, status_led_error_severity_t *severity)
{
    if (strcasecmp(text, "retryable") == 0 || strcasecmp(text, "warn") == 0 || strcasecmp(text, "warning") == 0) {
        *severity = STATUS_LED_ERROR_RETRYABLE;
    } else if (strcasecmp(text, "hard") == 0 || strcasecmp(text, "error") == 0 || strcasecmp(text, "fault") == 0) {
        *severity = STATUS_LED_ERROR_HARD;
    } else {
        return false;
    }
    return true;
}

static bool status_led_parse_calibration_strip(const char *text, status_led_strip_id_t *strip, uint8_t *count, uint8_t *first_led)
{
    if (strcasecmp(text, "status") == 0) {
        *strip = STATUS_LED_STRIP_STATUS;
        *count = STATUS_LED_STATUS_COUNT;
        *first_led = STATUS_LED_STATUS_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "ec11") == 0 ||
        strcasecmp(text, "knob") == 0 ||
        strcasecmp(text, "ring") == 0) {
        *strip = STATUS_LED_STRIP_EC11;
        *count = STATUS_LED_EC11_COUNT;
        *first_led = STATUS_LED_EC11_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "key") == 0) {
        *strip = STATUS_LED_STRIP_KEY;
        *count = STATUS_LED_KEY_COUNT;
        *first_led = STATUS_LED_KEY_FIRST_LED;
        return true;
    }
    if (strcasecmp(text, "edge") == 0) {
        *strip = STATUS_LED_STRIP_EDGE;
        *count = STATUS_LED_EDGE_COUNT;
        *first_led = STATUS_LED_EDGE_FIRST_LED;
        return true;
    }
    return false;
}

static bool status_led_parse_pixel_index(const char *text, uint8_t first_led, uint8_t count, uint8_t *zero_based_index)
{
    const char *cursor = text;
    bool led_prefixed = false;
    if (strncasecmp(cursor, "LED", 3) == 0) {
        cursor += 3;
        led_prefixed = true;
    }

    char *end = NULL;
    long value = strtol(cursor, &end, 10);
    if (cursor == end || *end != '\0') {
        return false;
    }

    long last_led = (long)first_led + (long)count - 1L;
    if (led_prefixed && value >= first_led && value <= last_led) {
        *zero_based_index = (uint8_t)(value - first_led);
        return true;
    }

    if (value >= 1 && value <= count) {
        *zero_based_index = (uint8_t)(value - 1);
        return true;
    }

    if (!led_prefixed && value >= first_led && value <= last_led) {
        *zero_based_index = (uint8_t)(value - first_led);
        return true;
    }

    return false;
}

static bool status_led_parse_color_token(const char *text, status_led_rgb_t *color)
{
    if (strcasecmp(text, "red") == 0) {
        *color = status_led_rgb(255, 0, 0);
    } else if (strcasecmp(text, "green") == 0) {
        *color = status_led_rgb(0, 255, 0);
    } else if (strcasecmp(text, "blue") == 0) {
        *color = status_led_rgb(0, 0, 255);
    } else if (strcasecmp(text, "white") == 0) {
        *color = status_led_rgb(255, 255, 255);
    } else if (strcasecmp(text, "off") == 0 || strcasecmp(text, "black") == 0) {
        *color = status_led_rgb(0, 0, 0);
    } else {
        return false;
    }
    return true;
}

static void status_led_run_pixel_test(
    status_led_strip_id_t strip,
    uint8_t index,
    status_led_rgb_t color,
    uint8_t percent)
{
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.test_mode = STATUS_LED_TEST_PIXEL;
        s_state.test_strip_mask = 1U << strip;
        s_state.test_pixel_strip = strip;
        s_state.test_pixel_index = index;
        s_state.test_pixel_color = color;
        s_state.test_pixel_percent = percent;
        s_state.test_started_ms = now_ms;
        s_state.output_disabled = false;
        s_state.low_power_disabled = false;
        s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
        status_led_set_last_reason_locked("test_pixel");
        xSemaphoreGive(s_mutex);
    }
    status_led_request_refresh();
}

static void status_led_print_status(void)
{
    status_led_state_t snapshot;
    status_led_strip_t strips[STATUS_LED_STRIP_COUNT];
    device_settings_snapshot_t device_settings = {0};
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        memcpy(strips, s_strips, sizeof(strips));
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    device_settings_get_snapshot(&device_settings);

    const uint8_t profile_cap_percent = status_led_profile_cap_percent_for(snapshot.profile, false);
    const uint8_t effective_cap_percent = snapshot.brightness_percent < profile_cap_percent
        ? snapshot.brightness_percent
        : profile_cap_percent;
    const uint32_t status_window_ms_left =
        now_ms < snapshot.status_window_until_ms ? snapshot.status_window_until_ms - now_ms : 0U;
    const uint32_t ble_confidence_ms_left =
        now_ms < snapshot.ble_confidence_until_ms ? snapshot.ble_confidence_until_ms - now_ms : 0U;
    const uint32_t oobe_confidence_ms_left =
        now_ms < snapshot.oobe_confidence_until_ms ? snapshot.oobe_confidence_until_ms - now_ms : 0U;
    const uint32_t charge_full_candidate_ms =
        snapshot.charge_full_candidate_since_ms != 0 && now_ms >= snapshot.charge_full_candidate_since_ms
            ? now_ms - snapshot.charge_full_candidate_since_ms
            : 0U;
    const uint8_t brightness_duty_255 =
        status_led_linear_percent_to_255(snapshot.brightness_percent);
    const uint8_t active_pwr = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_PWR]) ? 1U : 0U;
    const uint8_t active_ble = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_BLE]) ? 1U : 0U;
    const uint8_t active_rec = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_REC]) ? 1U : 0U;
    const uint8_t active_ai = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_AI]) ? 1U : 0U;
    const uint8_t active_ok = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_OK]) ? 1U : 0U;
    const uint8_t active_warn = status_led_rgb_is_on(snapshot.last_frame.status[STATUS_LED_SEM_WARN]) ? 1U : 0U;
    const uint8_t active_key =
        status_led_strip_has_light(snapshot.last_frame.key, STATUS_LED_KEY_COUNT) ? 1U : 0U;
    const uint8_t active_edge =
        status_led_strip_has_light(snapshot.last_frame.edge, STATUS_LED_EDGE_COUNT) ? 1U : 0U;

    printf(
        "~LED:STATUS detail=contract backend=rmt_ws2812_800khz refresh_ms=%u reset_us=300"
        " idle_refresh_ms=%u unchanged_tx_suppression=1 timing=ws2812_4020_compatible"
        " led_contract_rev=" STATUS_LED_CONTRACT_REV
        " factory_full_brightness=1 safety_full_brightness=1"
        " semantic_order=LED1:PWR,LED2:BLE,LED3:REC,LED4:AI,LED5:OK,LED6:WARN"
        " mapping_contract=" STATUS_LED_STATUS_KEY_MAPPING_CONTRACT
        " status_physical_map=" STATUS_LED_STATUS_PHYSICAL_MAP
        " key_physical_map=" STATUS_LED_KEY_PHYSICAL_MAP
        " separate_status_key_color_order=1 status_default_order=GRB key_default_order=GRB\n",
        STATUS_LED_REFRESH_MS,
        STATUS_LED_IDLE_REFRESH_MS);
    printf(
        "~LED:STATUS detail=brightness profile=%s effect_profile=product_v1"
        " profile_cap_percent=%u brightness_percent=%u effective_cap_percent=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " plugged_brightness_percent=%u battery_brightness_percent=%u active_power_brightness_percent=%u"
        " brightness_duty_255=%u"
        " user_brightness_is_hard_cap=1 profile_dimming_disabled=1\n",
        status_led_profile_name(snapshot.profile),
        profile_cap_percent,
        snapshot.brightness_percent,
        effective_cap_percent,
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        device_settings.plugged_brightness_percent,
        device_settings.battery_brightness_percent,
        snapshot.brightness_percent,
        brightness_duty_255);
    printf(
        "~LED:STATUS detail=strips"
        " strips=status:gpio%d:count%u:order%s:refsLED1..LED6,ec11:gpio%d:count%u:order%s:refsLED7..LED10+LED15..LED16+LED23..LED28,key:gpio%d:count%u:order%s:refsLED11..LED14,edge:gpio%d:count%u:order%s:refsLED17..LED22"
        " key_pin_contract=PWM_RGB_KEY_GPIO13 ec11_pin_contract=PWM_RGB_EC11_GPIO5 edge_pin_contract=PWM_RGB_Edge_GPIO4 gpio14_reserved=BAT_CHG_IO vdd_led_enable=always_on_assumed"
        "\n",
        (int)strips[STATUS_LED_STRIP_STATUS].gpio,
        (unsigned)strips[STATUS_LED_STRIP_STATUS].led_count,
        status_led_color_order_name(strips[STATUS_LED_STRIP_STATUS].color_order),
        (int)strips[STATUS_LED_STRIP_EC11].gpio,
        (unsigned)strips[STATUS_LED_STRIP_EC11].led_count,
        status_led_color_order_name(strips[STATUS_LED_STRIP_EC11].color_order),
        (int)strips[STATUS_LED_STRIP_KEY].gpio,
        (unsigned)strips[STATUS_LED_STRIP_KEY].led_count,
        status_led_color_order_name(strips[STATUS_LED_STRIP_KEY].color_order),
        (int)strips[STATUS_LED_STRIP_EDGE].gpio,
        (unsigned)strips[STATUS_LED_STRIP_EDGE].led_count,
        status_led_color_order_name(strips[STATUS_LED_STRIP_EDGE].color_order));
    printf(
        "~LED:STATUS detail=state ble=%s rec_active=%u rec_source=%s rec_level=%u processing=%u"
        " error_domain=%s error_severity=%s output_disabled=%u low_power_disabled=%u\n",
        status_led_ble_name(snapshot.ble_state),
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source),
        snapshot.recording_level_percent,
        snapshot.processing_active ? 1U : 0U,
        status_led_error_domain_name(snapshot.error_domain),
        status_led_error_severity_name(snapshot.error_severity),
        snapshot.output_disabled ? 1U : 0U,
        snapshot.low_power_disabled ? 1U : 0U);
    printf(
        "~LED:STATUS detail=power battery_valid=%u battery_level=%u battery_mv=%" PRIu32
        " external_power=%u charging=%u full=%u raw_charging=%u raw_full=%u"
        " full_latched=%u full_candidate_ms=%" PRIu32
        " full_debounce_ms=%u full_min_mv=%u full_min_percent=%u"
        " status_window_ms_left=%" PRIu32 " ble_confidence_ms_left=%" PRIu32
        " oobe_confidence_ms_left=%" PRIu32 " last_transition_ms=%" PRIu32
        " current_ma=%" PRIu32 " current_budget_ma=%" PRIu32 "\n",
        snapshot.battery_valid ? 1U : 0U,
        snapshot.battery_level_percent,
        snapshot.battery_mv,
        snapshot.external_power_present ? 1U : 0U,
        snapshot.charging ? 1U : 0U,
        snapshot.full ? 1U : 0U,
        snapshot.raw_charging ? 1U : 0U,
        snapshot.raw_full ? 1U : 0U,
        snapshot.charge_full_latched ? 1U : 0U,
        charge_full_candidate_ms,
        STATUS_LED_CHARGE_FULL_DEBOUNCE_MS,
        STATUS_LED_CHARGE_FULL_MIN_MV,
        STATUS_LED_CHARGE_FULL_MIN_PERCENT,
        status_window_ms_left,
        ble_confidence_ms_left,
        oobe_confidence_ms_left,
        snapshot.last_transition_ms,
        snapshot.last_estimated_current_ma,
        snapshot.last_current_budget_ma);
    printf(
        "~LED:STATUS detail=rgb"
        " status_rgb=PWR:%u,%u,%u;BLE:%u,%u,%u;REC:%u,%u,%u;AI:%u,%u,%u;OK:%u,%u,%u;WARN:%u,%u,%u\n",
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].r,
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].g,
        snapshot.last_frame.status[STATUS_LED_SEM_PWR].b,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].r,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].g,
        snapshot.last_frame.status[STATUS_LED_SEM_BLE].b,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].r,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].g,
        snapshot.last_frame.status[STATUS_LED_SEM_REC].b,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].r,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].g,
        snapshot.last_frame.status[STATUS_LED_SEM_AI].b,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].r,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].g,
        snapshot.last_frame.status[STATUS_LED_SEM_OK].b,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].r,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].g,
        snapshot.last_frame.status[STATUS_LED_SEM_WARN].b);
    printf(
        "~LED:STATUS profile=%s detail=summary profile_cap_percent=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " ble=%s rec_active=%u rec_source=%s processing=%u"
        " error_domain=%s error_severity=%s battery_level=%u charging=%u full=%u"
        " active_flags=PWR:%u,BLE:%u,REC:%u,AI:%u,OK:%u,WARN:%u,KEY:%u,EDGE:%u"
        " key_mask=0x%02x test_mode=%u test_strip_mask=0x%02x last_reason=%s\n",
        status_led_profile_name(snapshot.profile),
        profile_cap_percent,
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        status_led_ble_name(snapshot.ble_state),
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source),
        snapshot.processing_active ? 1U : 0U,
        status_led_error_domain_name(snapshot.error_domain),
        status_led_error_severity_name(snapshot.error_severity),
        snapshot.battery_level_percent,
        snapshot.charging ? 1U : 0U,
        snapshot.full ? 1U : 0U,
        active_pwr,
        active_ble,
        active_rec,
        active_ai,
        active_ok,
        active_warn,
        active_key,
        active_edge,
        snapshot.key_pressed_mask,
        (unsigned)snapshot.test_mode,
        (unsigned)snapshot.test_strip_mask,
        snapshot.last_reason);
    fflush(stdout);
}

static void status_led_print_budget(void)
{
    status_led_state_t snapshot;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    printf(
        "~LED:BUDGET profile=%s cap_current_ma=%" PRIu32 " estimated_current_ma=%" PRIu32
        " profile_cap_percent=%u user_brightness_percent=%u effective_cap_percent=%u factory_brightness_percent=%u"
        " user_brightness_duty_255=%u"
        " budget_scale_percent=%u budget_limited_by_current=%u"
        " configured_profile_budget_ma=%" PRIu32 " factory_budget_ma=%u"
        " product_effect_profile=1 user_brightness_is_hard_cap=1 profile_dimming_disabled=1 off_zero_brightness=1 safety_full_brightness=1"
        " per_led_full_white_ma=60 vdd_led_enable=always_on_assumed\n",
        status_led_profile_name(snapshot.profile),
        snapshot.last_current_budget_ma,
        snapshot.last_estimated_current_ma,
        status_led_profile_cap_percent_for(snapshot.profile, false),
        snapshot.brightness_percent,
        snapshot.brightness_percent < status_led_profile_cap_percent_for(snapshot.profile, false)
            ? snapshot.brightness_percent
            : status_led_profile_cap_percent_for(snapshot.profile, false),
        STATUS_LED_FULL_BRIGHTNESS_PERCENT,
        status_led_linear_percent_to_255(snapshot.brightness_percent),
        snapshot.last_budget_scale_percent,
        snapshot.current_limited_by_budget ? 1U : 0U,
        status_led_profile_budget_ma_for(snapshot.profile, false),
        STATUS_LED_FULL_BRIGHTNESS_BUDGET_MA);
    fflush(stdout);
}

static void status_led_print_privacy(void)
{
    status_led_state_t snapshot;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = s_state;
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    printf(
        "~LED:PRIVACY rec_allowed=%u rec_active=%u capture_source=%s rec_not_available_shows=WARN+REC\n",
        snapshot.recording_active && snapshot.rec_source != STATUS_LED_REC_SOURCE_NOT_AVAILABLE ? 1U : 0U,
        snapshot.recording_active ? 1U : 0U,
        status_led_rec_source_name(snapshot.rec_source));
    fflush(stdout);
}

static void status_led_preview_state(const char *state)
{
    uint32_t now_ms = status_led_now_ms();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_state.output_disabled = false;
    s_state.low_power_disabled = false;
    s_state.test_mode = STATUS_LED_TEST_NONE;
    s_state.status_window_until_ms = now_ms + STATUS_LED_STATUS_WINDOW_MS;
    s_state.last_power_poll_ms = now_ms;
    s_state.last_transition_ms = now_ms;
    status_led_set_last_reason_locked("preview");

    if (strcasecmp(state, "ready") == 0 || strcasecmp(state, "connected") == 0) {
        s_state.ble_state = STATUS_LED_BLE_CONNECTED;
        s_state.ble_confidence_until_ms = now_ms + STATUS_LED_BLE_CONFIDENCE_MS;
        s_state.battery_valid = true;
        s_state.battery_level_percent = 80;
        s_state.battery_mv = 4000;
        s_state.external_power_present = false;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "pairing") == 0) {
        s_state.ble_state = STATUS_LED_BLE_PAIRING;
    } else if (strcasecmp(state, "reconnect") == 0 || strcasecmp(state, "reconnecting") == 0) {
        s_state.ble_state = STATUS_LED_BLE_RECONNECTING;
    } else if (strcasecmp(state, "capture") == 0 || strcasecmp(state, "device_mic") == 0) {
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DEVICE_MIC;
    } else if (strcasecmp(state, "desktop_mic") == 0) {
        s_state.recording_active = true;
        s_state.rec_source = STATUS_LED_REC_SOURCE_DESKTOP_MIC;
    } else if (strcasecmp(state, "rec_not_available") == 0 || strcasecmp(state, "not_available") == 0) {
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NOT_AVAILABLE;
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_REC;
        s_state.error_severity = STATUS_LED_ERROR_RETRYABLE;
        s_state.error_started_ms = now_ms;
        s_state.error_until_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
    } else if (strcasecmp(state, "processing") == 0 || strcasecmp(state, "thinking") == 0 || strcasecmp(state, "ota") == 0) {
        s_state.processing_active = true;
        s_state.processing_started_ms = now_ms;
    } else if (strcasecmp(state, "ok") == 0 || strcasecmp(state, "success") == 0) {
        s_state.ok_started_ms = now_ms;
        s_state.ok_until_ms = now_ms + STATUS_LED_OK_TOTAL_MS;
    } else if (strcasecmp(state, "low_battery") == 0) {
        s_state.battery_valid = true;
        s_state.battery_level_percent = 15;
        s_state.battery_mv = 3500;
        s_state.external_power_present = false;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "critical_battery") == 0) {
        s_state.battery_valid = true;
        s_state.battery_level_percent = 5;
        s_state.battery_mv = 3300;
        s_state.external_power_present = false;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "charging") == 0) {
        s_state.external_power_present = true;
        s_state.charging = true;
        s_state.full = false;
        s_state.raw_charging = true;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
    } else if (strcasecmp(state, "full") == 0) {
        s_state.external_power_present = true;
        s_state.charging = false;
        s_state.full = true;
        s_state.raw_charging = false;
        s_state.raw_full = true;
        s_state.charge_full_latched = true;
        s_state.charge_full_candidate_since_ms = now_ms;
    } else if (strcasecmp(state, "sleep") == 0) {
        s_state.low_power_disabled = true;
        s_state.output_disabled = true;
    } else if (strcasecmp(state, "clear") == 0 || strcasecmp(state, "off") == 0) {
        s_state.ble_state = STATUS_LED_BLE_DISCONNECTED;
        s_state.ble_confidence_until_ms = 0;
        s_state.oobe_confidence_until_ms = 0;
        s_state.recording_active = false;
        s_state.rec_source = STATUS_LED_REC_SOURCE_NONE;
        s_state.processing_active = false;
        s_state.battery_valid = false;
        s_state.battery_level_percent = 0;
        s_state.battery_mv = 0;
        s_state.external_power_present = false;
        s_state.charging = false;
        s_state.full = false;
        s_state.raw_charging = false;
        s_state.raw_full = false;
        s_state.charge_full_latched = false;
        s_state.charge_full_candidate_since_ms = 0;
        s_state.error_domain = STATUS_LED_ERROR_DOMAIN_NONE;
        s_state.error_until_ms = 0;
        s_state.ok_until_ms = 0;
    } else {
        ESP_LOGW(TAG, "LED preview unknown state: %s", state);
    }
    s_state.last_transition_ms = now_ms;
    xSemaphoreGive(s_mutex);
    status_led_request_refresh();
}

bool status_led_consume_usb_command(const char *line)
{
    const char *command = status_led_strip_prefix(line);
    if (command == NULL) {
        return false;
    }

    if (strcmp(command, "STATUS") == 0) {
        status_led_print_status();
        return true;
    }
    if (strcmp(command, "BUDGET") == 0) {
        status_led_print_budget();
        return true;
    }
    if (strcmp(command, "PRIVACY") == 0) {
        status_led_print_privacy();
        return true;
    }
    if (strcmp(command, "OFF") == 0) {
        status_led_force_manual_off();
        ESP_LOGI(TAG, "LED output manually forced off until the next status/key event");
        return true;
    }
    if (strcmp(command, "WAKE") == 0) {
        status_led_show_status_window("usb_wake");
        ESP_LOGI(TAG, "LED status window requested");
        return true;
    }

    if (strncmp(command, "BRIGHTNESS ", strlen("BRIGHTNESS ")) == 0) {
        const char *arg = command + strlen("BRIGHTNESS ");
        char *end = NULL;
        long value = strtol(arg, &end, 10);
        char *parsed_end = end;
        while (end != NULL && *end == ' ') {
            end++;
        }
        if (parsed_end == arg || end == NULL || *end != '\0' || value < 0 || value > 100) {
            ESP_LOGW(TAG, "LED brightness must be 0..100: %s", arg);
            return true;
        }
        uint8_t brightness = (uint8_t)value;
        status_led_profile_t profile = STATUS_LED_PROFILE_STANDARD;
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.brightness_percent = brightness;
            profile = s_state.profile;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.status_window_until_ms = status_led_now_ms() + STATUS_LED_STATUS_WINDOW_MS;
            status_led_set_last_reason_locked("brightness");
            xSemaphoreGive(s_mutex);
        }
        (void)device_settings_set_brightness_profiles(brightness, brightness);
        status_led_save_brightness(brightness);
        status_led_request_refresh();
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_PROFILE, DIAG_SEV_INFO,
                 (uint32_t)profile, brightness, 1, 0);
        ESP_LOGI(TAG, "LED brightness=%u", (unsigned)brightness);
        return true;
    }

    if (strncmp(command, "PROFILE ", strlen("PROFILE ")) == 0) {
        status_led_profile_t profile;
        const char *arg = command + strlen("PROFILE ");
        if (!status_led_parse_profile(arg, &profile)) {
            ESP_LOGW(TAG, "LED profile unknown: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.profile = profile;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            s_state.status_window_until_ms = status_led_now_ms() + STATUS_LED_STATUS_WINDOW_MS;
            status_led_set_last_reason_locked("profile");
            xSemaphoreGive(s_mutex);
        }
        status_led_save_profile(profile);
        status_led_request_refresh();
        diag_log(DIAG_SRC_STATUS_LED, DIAG_LED_PROFILE, DIAG_SEV_INFO, (uint32_t)profile, 0, 0, 0);
        ESP_LOGI(TAG, "LED profile=%s", status_led_profile_name(profile));
        return true;
    }

    if (strncmp(command, "TEST:RGBW", strlen("TEST:RGBW")) == 0) {
        const char *arg = command + strlen("TEST:RGBW");
        while (*arg == ' ') {
            arg++;
        }
        uint8_t mask = status_led_parse_strip_mask(arg);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED TEST:RGBW unknown strip: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_RGBW;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            status_led_set_last_reason_locked("test_rgbw");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED RGBW calibration running mask=0x%02x", mask);
        return true;
    }

    if (strncmp(command, "TEST:MAP", strlen("TEST:MAP")) == 0) {
        const char *arg = command + strlen("TEST:MAP");
        while (*arg == ' ') {
            arg++;
        }
        uint8_t mask = status_led_parse_strip_mask(arg);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED TEST:MAP unknown strip: %s", arg);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_MAP;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            status_led_set_last_reason_locked("test_map");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED map test running mask=0x%02x status_order=PWR,BLE,REC,AI,OK,WARN ec11_order=LED7..LED10+LED15..LED16+LED23..LED28 key_order=KEY1,KEY2,KEY3,KEY4 edge_order=LED17..LED22", mask);
        return true;
    }

    if (strncmp(command, "CHASE", strlen("CHASE")) == 0) {
        char strip_text[48] = "status,key";
        unsigned step_ms = STATUS_LED_CHASE_DEFAULT_STEP_MS;
        const char *arg = command + strlen("CHASE");
        while (*arg == ' ') {
            arg++;
        }
        if (*arg != '\0') {
            (void)sscanf(arg, "%47s %u", strip_text, &step_ms);
        }
        if (step_ms < 80U) {
            step_ms = 80U;
        } else if (step_ms > 2000U) {
            step_ms = 2000U;
        }
        uint8_t mask = status_led_parse_strip_mask(strip_text);
        if (mask == 0) {
            ESP_LOGW(TAG, "LED CHASE unknown strip: %s", strip_text);
            return true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            s_state.test_mode = STATUS_LED_TEST_CHASE;
            s_state.test_strip_mask = mask;
            s_state.test_started_ms = status_led_now_ms();
            s_state.test_step_ms = (uint16_t)step_ms;
            s_state.output_disabled = false;
            s_state.low_power_disabled = false;
            status_led_set_last_reason_locked("test_chase");
            xSemaphoreGive(s_mutex);
        }
        status_led_request_refresh();
        ESP_LOGI(TAG, "LED chase running mask=0x%02x step_ms=%u full_brightness=1", mask, step_ms);
        return true;
    }

    if (strncmp(command, "TEST:PIXEL ", strlen("TEST:PIXEL ")) == 0) {
        char strip_text[16] = {0};
        char led_text[16] = {0};
        char color_text[16] = {0};
        unsigned percent = 25U;
        int fields = sscanf(
            command + strlen("TEST:PIXEL "),
            "%15s %15s %15s %u",
            strip_text,
            led_text,
            color_text,
            &percent);
        if (fields < 3) {
            ESP_LOGW(TAG, "LED TEST:PIXEL requires <status|ec11|knob|ring|key|edge> <LEDn|index> <red|green|blue|white|off> [percent]");
            return true;
        }

        status_led_strip_id_t strip;
        uint8_t count = 0;
        uint8_t first_led = 0;
        uint8_t index = 0;
        status_led_rgb_t color = {0};
        if (!status_led_parse_calibration_strip(strip_text, &strip, &count, &first_led) ||
            !status_led_parse_pixel_index(led_text, first_led, count, &index) ||
            !status_led_parse_color_token(color_text, &color) ||
            percent > 100U) {
            ESP_LOGW(TAG, "LED TEST:PIXEL invalid args: strip=%s led=%s color=%s percent=%u", strip_text, led_text, color_text, percent);
            return true;
        }

        if (strcasecmp(color_text, "off") == 0 || strcasecmp(color_text, "black") == 0) {
            percent = 0U;
        }
        status_led_run_pixel_test(strip, index, color, (uint8_t)percent);
        bool status_key_only = strip == STATUS_LED_STRIP_STATUS || strip == STATUS_LED_STRIP_KEY;
        ESP_LOGI(
            TAG,
            "LED pixel calibration strip=%s led=LED%u index=%u color=%s percent=%u status_key_only=%u ec11_edge_touched=%u ec11_edge_untouched=%u mapping_contract=%s",
            strip_text,
            (unsigned)(first_led + index),
            (unsigned)index,
            color_text,
            percent,
            status_key_only ? 1U : 0U,
            status_key_only ? 0U : 1U,
            status_key_only ? 1U : 0U,
            STATUS_LED_STATUS_KEY_MAPPING_CONTRACT);
        return true;
    }

    if (strncmp(command, "PREVIEW ", strlen("PREVIEW ")) == 0) {
        status_led_preview_state(command + strlen("PREVIEW "));
        ESP_LOGI(TAG, "LED preview=%s", command + strlen("PREVIEW "));
        return true;
    }

    if (strncmp(command, "ERROR ", strlen("ERROR ")) == 0) {
        char domain_text[16] = {0};
        char severity_text[16] = {0};
        if (sscanf(command + strlen("ERROR "), "%15s %15s", domain_text, severity_text) != 2) {
            ESP_LOGW(TAG, "LED ERROR requires domain severity");
            return true;
        }
        status_led_error_domain_t domain;
        status_led_error_severity_t severity;
        if (!status_led_parse_domain(domain_text, &domain) ||
            !status_led_parse_severity(severity_text, &severity)) {
            ESP_LOGW(TAG, "LED ERROR unknown domain/severity: %s %s", domain_text, severity_text);
            return true;
        }
        status_led_set_error(domain, severity, "usb_error_preview");
        return true;
    }

    ESP_LOGW(TAG, "LED unknown command: %s", command);
    return true;
}
