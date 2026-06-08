#include "voice_key_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "power_manager.h"

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
#define VOICE_KEY_INPUT_LEGACY_ES8311_BOARD 1
#else
#define VOICE_KEY_INPUT_LEGACY_ES8311_BOARD 0
#endif

#ifndef VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
#define VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER VOICE_KEY_INPUT_LEGACY_ES8311_BOARD
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
#include "driver/i2c_master.h"
#endif
#include "esp_check.h"
#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#endif
#include "esp_log.h"

#include "diag_log.h"
#include "watchdog_platform.h"

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
#include "audio_capture_platform.h"
#endif
#include "board_pins.h"

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
#define VOICE_KEY_INPUT_ALT_I2C_PORT   (1)
#define VOICE_KEY_INPUT_ALT_I2C_SDA_IO (38)
#define VOICE_KEY_INPUT_ALT_I2C_SCL_IO (48)
#define VOICE_KEY_INPUT_FALLBACK_SDA_IO (47)
#define VOICE_KEY_INPUT_FALLBACK_SCL_IO (48)
#define VOICE_KEY_INPUT_INT_IO         (46)
#define VOICE_KEY_INPUT_KEY1_MASK_IO0_4 (1U << 4)
#define VOICE_KEY_INPUT_KEY1_MASK_IO0_5 (1U << 5)
#define VOICE_KEY_INPUT_EXPANDER_KEY_MASKS (VOICE_KEY_INPUT_KEY1_MASK_IO0_4 | VOICE_KEY_INPUT_KEY1_MASK_IO0_5)
#define VOICE_KEY_INPUT_I2C_TIMEOUT_MS (100)
#define VOICE_KEY_INPUT_PROBE_RETRY_COUNT (4)
#define VOICE_KEY_INPUT_PROBE_RETRY_DELAY_MS (80)
#define VOICE_KEY_INPUT_ADDR           ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000
#define VOICE_KEY_INPUT_ALL_MASK       (0xFFFFU)
#endif

#if VOICE_KEY_INPUT_LEGACY_ES8311_BOARD
#define VOICE_KEY_INPUT_DIRECT_GPIO    GPIO_NUM_0
#define VOICE_KEY_INPUT_DIRECT_LABEL   "gpio0.boot"
#else
#define VOICE_KEY_INPUT_DIRECT_GPIO    BOARD_PINS_EC11_KEY_IO
#define VOICE_KEY_INPUT_DIRECT_LABEL   "ec11_key.gpio18"
#endif
#define VOICE_KEY_INPUT_POLL_MS        (10)
#define VOICE_KEY_INPUT_DEBOUNCE_MS    (30)
#define VOICE_KEY_INPUT_DEBOUNCE_THRESHOLD \
    ((VOICE_KEY_INPUT_DEBOUNCE_MS + VOICE_KEY_INPUT_POLL_MS - 1) / VOICE_KEY_INPUT_POLL_MS)
#define VOICE_KEY_INPUT_EVENT_QUEUE_LENGTH (8)
#define VOICE_KEY_INPUT_CLICK_MAX_MS (700)
#define VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS (200)
#define VOICE_KEY_INPUT_LONG_PRESS_IGNORE_MS (1200)
#define VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD_MS (2000)
#define VOICE_KEY_INPUT_DEBUG_RAW 1u
#define VOICE_KEY_INPUT_DEBUG_STABLE 2u
#define VOICE_KEY_INPUT_DEBUG_SOURCE_DIRECT_GPIO 1u
#define VOICE_KEY_INPUT_DEBUG_SOURCE_LEGACY_IO0_4 2u
#define VOICE_KEY_INPUT_DEBUG_SOURCE_LEGACY_IO0_5 3u

static const char *TAG = "voice_key_input";

typedef struct {
    const char *label;
    uint32_t debug_source;
    bool active_low;
    bool idle_level_valid;
    bool idle_level_high;
    bool last_sample_high;
    bool stable_level_high;
    uint8_t stable_count;
    bool pressed;
    uint32_t pressed_ms;
    bool pending_single_click;
    uint32_t pending_click_ms;
    bool long_press_reported;
} voice_key_button_state_t;

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
typedef struct {
    const char *label;
    bool use_shared_audio_bus;
    i2c_port_num_t i2c_port;
    gpio_num_t sda_io;
    gpio_num_t scl_io;
} voice_key_input_bus_candidate_t;
#endif

static bool s_started;
#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
static i2c_master_bus_handle_t s_i2c_bus_handle;
static esp_io_expander_handle_t s_io_expander;
#endif
static TaskHandle_t s_poll_task_handle;
static SemaphoreHandle_t s_toggle_event_sem;
static SemaphoreHandle_t s_recovery_event_sem;
static volatile bool s_recording_output_enabled;
static volatile bool s_recording_output_change_seen;
static volatile TickType_t s_recording_output_last_change_tick;
#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
static bool s_prev_input_valid;
static uint32_t s_prev_input_levels;
static bool s_owns_i2c_bus;
static const char *s_selected_bus_label;
static bool s_expander_available;
static voice_key_button_state_t s_expander_io4_state = {
    .label = "xl9555.io0_4",
    .debug_source = VOICE_KEY_INPUT_DEBUG_SOURCE_LEGACY_IO0_4,
    .active_low = true,
};
static voice_key_button_state_t s_expander_io5_state = {
    .label = "xl9555.io0_5",
    .debug_source = VOICE_KEY_INPUT_DEBUG_SOURCE_LEGACY_IO0_5,
    .active_low = true,
};
#endif
static voice_key_button_state_t s_direct_gpio_state = {
    .label = VOICE_KEY_INPUT_DIRECT_LABEL,
    .debug_source = VOICE_KEY_INPUT_DEBUG_SOURCE_DIRECT_GPIO,
    .active_low = true,
};

static void voice_key_input_debug_log(
    uint32_t kind,
    const voice_key_button_state_t *button,
    bool raw_high,
    uint32_t detail)
{
    if (button == NULL || !diag_log_input_debug_enabled()) {
        return;
    }

    diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_INPUT_DEBUG, DIAG_SEV_INFO,
             kind,
             button->debug_source,
             raw_high ? 1u : 0u,
             detail);
}

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
/* Legacy board compatibility only: old hardware used a TCA9555/XL9555 expander
 * and BOOT GPIO fallback. The active product path uses the EC11 push key for
 * recording gestures so KEY1-KEY4 can remain logical custom keys. */
static const voice_key_input_bus_candidate_t s_bus_candidates[] = {
    {
        .label = "shared_sda1_scl1",
        .use_shared_audio_bus = true,
        .i2c_port = I2C_NUM_0,
        .sda_io = GPIO_NUM_4,
        .scl_io = GPIO_NUM_5,
    },
    {
        .label = "alt_sda2_scl2",
        .use_shared_audio_bus = false,
        .i2c_port = VOICE_KEY_INPUT_ALT_I2C_PORT,
        .sda_io = VOICE_KEY_INPUT_ALT_I2C_SDA_IO,
        .scl_io = VOICE_KEY_INPUT_ALT_I2C_SCL_IO,
    },
    {
        .label = "fallback_gpio47_gpio48",
        .use_shared_audio_bus = false,
        .i2c_port = VOICE_KEY_INPUT_ALT_I2C_PORT,
        .sda_io = VOICE_KEY_INPUT_FALLBACK_SDA_IO,
        .scl_io = VOICE_KEY_INPUT_FALLBACK_SCL_IO,
    },
};
#endif

static void voice_key_input_record_toggle_event(const char *source, uint32_t event_type, const char *edge_label)
{
    if (s_toggle_event_sem == NULL) {
        ESP_LOGW(TAG, "%s %s toggle dropped: event queue unavailable", source, edge_label);
        diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_QUEUE_DROP, DIAG_SEV_WARN, event_type, 1, 0, 0);
        return;
    }

    if (xSemaphoreGive(s_toggle_event_sem) == pdTRUE) {
        ESP_LOGI(TAG, "%s %s toggle detected", source, edge_label);
        diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_PRESS, DIAG_SEV_INFO, event_type, 0, 0, 0);
    } else {
        ESP_LOGW(TAG, "%s %s toggle dropped: event queue full", source, edge_label);
        diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_QUEUE_DROP, DIAG_SEV_WARN, event_type, 2, 0, 0);
    }
}

static void voice_key_input_record_recovery_event(const char *source)
{
    if (s_recovery_event_sem == NULL) {
        ESP_LOGW(TAG, "%s double-click recovery dropped: event queue unavailable", source);
        return;
    }

    if (xSemaphoreGive(s_recovery_event_sem) == pdTRUE) {
        ESP_LOGW(TAG, "%s double-click recovery detected", source);
        diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_PRESS, DIAG_SEV_WARN, 2, VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS, 0, 0);
    } else {
        ESP_LOGW(TAG, "%s double-click recovery dropped: event queue full", source);
        diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_QUEUE_DROP, DIAG_SEV_WARN, 2, 2, 0, 0);
    }
}

static bool voice_key_input_recovery_allowed(void)
{
    if (s_recording_output_enabled) {
        return false;
    }
    if (!s_recording_output_change_seen) {
        return true;
    }

    TickType_t elapsed = xTaskGetTickCount() - s_recording_output_last_change_tick;
    return elapsed >= pdMS_TO_TICKS(VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD_MS);
}

static void voice_key_input_poll_pending_single_click(voice_key_button_state_t *button)
{
    if (button == NULL || button->pressed || !button->pending_single_click) {
        return;
    }

    button->pending_click_ms += VOICE_KEY_INPUT_POLL_MS;
    if (button->pending_click_ms < VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS) {
        return;
    }

    button->pending_single_click = false;
    button->pending_click_ms = 0;
    voice_key_input_record_toggle_event(button->label, 1, "single-click");
}

static void voice_key_input_handle_button_sample(voice_key_button_state_t *button, bool raw_high)
{
    if (button == NULL) {
        return;
    }

    if (!button->idle_level_valid) {
        button->idle_level_valid = true;
        button->idle_level_high = button->active_low;
        button->last_sample_high = raw_high;
        button->stable_level_high = raw_high;
        button->stable_count = 1;
        button->pressed = false;
        button->pressed_ms = 0;
        button->pending_single_click = false;
        button->pending_click_ms = 0;
        button->long_press_reported = false;
        ESP_LOGI(
            TAG,
            "recording gesture key idle level detected: source=%s raw_high=%d pressed_when=%s",
            button->label,
            raw_high ? 1 : 0,
            button->idle_level_high ? "low" : "high");
        return;
    }

    if (raw_high == button->last_sample_high) {
        if (button->stable_count < UINT8_MAX) {
            button->stable_count++;
        }
    } else {
        voice_key_input_debug_log(
            VOICE_KEY_INPUT_DEBUG_RAW,
            button,
            raw_high,
            button->stable_level_high ? 1u : 0u);
        button->last_sample_high = raw_high;
        button->stable_count = 1;
        return;
    }

    if (button->stable_count < VOICE_KEY_INPUT_DEBOUNCE_THRESHOLD) {
        return;
    }

    if (raw_high != button->stable_level_high) {
        button->stable_level_high = raw_high;
        ESP_LOGI(TAG, "recording gesture key level changed: source=%s raw_high=%d", button->label, raw_high ? 1 : 0);
        bool pressed = raw_high != button->idle_level_high;
        voice_key_input_debug_log(
            VOICE_KEY_INPUT_DEBUG_STABLE,
            button,
            raw_high,
            pressed ? 1u : 0u);
    }

    bool pressed = raw_high != button->idle_level_high;
    if (pressed && !button->pressed) {
        button->pressed_ms = 0;
        button->long_press_reported = false;
        power_manager_record_activity("ec11_key_press");
    } else if (!pressed && button->pressed) {
        if (!button->long_press_reported && button->pressed_ms <= VOICE_KEY_INPUT_CLICK_MAX_MS) {
            if (button->pending_single_click) {
                if (voice_key_input_recovery_allowed()) {
                    button->pending_single_click = false;
                    button->pending_click_ms = 0;
                    voice_key_input_record_recovery_event(button->label);
                } else {
                    ESP_LOGI(
                        TAG,
                        "%s consecutive short click kept as recording toggle: recovery_idle_guard_ms=%d",
                        button->label,
                        VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD_MS);
                    voice_key_input_record_toggle_event(button->label, 1, "single-click");
                    button->pending_single_click = true;
                    button->pending_click_ms = 0;
                }
            } else {
                button->pending_single_click = true;
                button->pending_click_ms = 0;
                ESP_LOGI(TAG, "%s single click pending for double-click window", button->label);
            }
        } else if (button->long_press_reported) {
            ESP_LOGI(TAG, "%s long press released without recording gesture", button->label);
        }
        button->pressed_ms = 0;
        button->long_press_reported = false;
    } else if (pressed && !button->long_press_reported) {
        uint32_t next_pressed_ms = button->pressed_ms + VOICE_KEY_INPUT_POLL_MS;
        button->pressed_ms = next_pressed_ms;
        if (next_pressed_ms >= VOICE_KEY_INPUT_LONG_PRESS_IGNORE_MS) {
            button->long_press_reported = true;
            button->pending_single_click = false;
            button->pending_click_ms = 0;
            ESP_LOGI(TAG, "%s long press reserved for power control: hold_ms=%" PRIu32, button->label, next_pressed_ms);
            diag_log(DIAG_SRC_VOICE_KEY, DIAG_VKEY_PRESS, DIAG_SEV_INFO, 3, next_pressed_ms, 0, 0);
        }
    }
    button->pressed = pressed;
    voice_key_input_poll_pending_single_click(button);
}

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
static esp_err_t voice_key_input_probe_candidate(
    const voice_key_input_bus_candidate_t *candidate,
    i2c_master_bus_handle_t *bus_handle_out,
    bool *owns_bus_out)
{
    if (candidate->use_shared_audio_bus) {
        i2c_master_bus_handle_t shared_bus = audio_capture_get_i2c_bus_handle();
        if (shared_bus == NULL) {
            ESP_LOGI(TAG, "skip candidate=%s: shared audio bus unavailable", candidate->label);
            return ESP_ERR_NOT_FOUND;
        }

        esp_err_t probe_ret = i2c_master_probe(shared_bus, VOICE_KEY_INPUT_ADDR, VOICE_KEY_INPUT_I2C_TIMEOUT_MS);
        if (probe_ret != ESP_OK) {
            ESP_LOGI(TAG, "probe candidate=%s addr=0x%02x failed: %s", candidate->label, VOICE_KEY_INPUT_ADDR, esp_err_to_name(probe_ret));
            return probe_ret;
        }

        *bus_handle_out = shared_bus;
        *owns_bus_out = false;
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = candidate->i2c_port,
        .sda_io_num = candidate->sda_io,
        .scl_io_num = candidate->scl_io,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "create candidate=%s failed: %s", candidate->label, esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_master_probe(bus_handle, VOICE_KEY_INPUT_ADDR, VOICE_KEY_INPUT_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "probe candidate=%s addr=0x%02x failed: %s", candidate->label, VOICE_KEY_INPUT_ADDR, esp_err_to_name(ret));
        i2c_del_master_bus(bus_handle);
        return ret;
    }

    *bus_handle_out = bus_handle;
    *owns_bus_out = true;
    return ESP_OK;
}

static esp_err_t voice_key_input_expander_init(void)
{
    esp_err_t last_err = ESP_ERR_NOT_FOUND;
    for (int attempt = 1; attempt <= VOICE_KEY_INPUT_PROBE_RETRY_COUNT; ++attempt) {
        for (size_t i = 0; i < sizeof(s_bus_candidates) / sizeof(s_bus_candidates[0]); ++i) {
            const voice_key_input_bus_candidate_t *candidate = &s_bus_candidates[i];
            bool owns_bus = false;
            i2c_master_bus_handle_t bus_handle = NULL;
            esp_err_t probe_ret = voice_key_input_probe_candidate(candidate, &bus_handle, &owns_bus);
            if (probe_ret != ESP_OK) {
                last_err = probe_ret;
                continue;
            }

            s_i2c_bus_handle = bus_handle;
            s_owns_i2c_bus = owns_bus;
            s_selected_bus_label = candidate->label;
            ESP_LOGI(
                TAG,
                "xl9555 candidate selected: %s sda=%d scl=%d shared=%s attempt=%d/%d",
                candidate->label,
                candidate->sda_io,
                candidate->scl_io,
                owns_bus ? "no" : "yes",
                attempt,
                VOICE_KEY_INPUT_PROBE_RETRY_COUNT);
            break;
        }

        if (s_i2c_bus_handle != NULL) {
            break;
        }

        if (attempt < VOICE_KEY_INPUT_PROBE_RETRY_COUNT) {
            ESP_LOGW(
                TAG,
                "xl9555 probe retry scheduled: attempt=%d/%d delay_ms=%d last_err=%s",
                attempt,
                VOICE_KEY_INPUT_PROBE_RETRY_COUNT,
                VOICE_KEY_INPUT_PROBE_RETRY_DELAY_MS,
                esp_err_to_name(last_err));
            vTaskDelay(pdMS_TO_TICKS(VOICE_KEY_INPUT_PROBE_RETRY_DELAY_MS));
        }
    }

    ESP_RETURN_ON_FALSE(s_i2c_bus_handle != NULL, last_err, TAG, "no xl9555 candidate bus responded after retries");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca95xx_16bit(
            s_i2c_bus_handle,
            VOICE_KEY_INPUT_ADDR,
            &s_io_expander),
        TAG,
        "create xl9555 failed");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(s_io_expander, VOICE_KEY_INPUT_EXPANDER_KEY_MASKS, IO_EXPANDER_INPUT),
        TAG,
        "set expander key inputs failed");

    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << VOICE_KEY_INPUT_INT_IO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "int gpio config failed");
    s_expander_available = true;
    return ESP_OK;
}
#endif

static esp_err_t voice_key_input_direct_gpio_init(void)
{
    gpio_config_t direct_cfg = {
        .pin_bit_mask = 1ULL << VOICE_KEY_INPUT_DIRECT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&direct_cfg), TAG, "direct gpio config failed");
    return ESP_OK;
}

static void voice_key_input_poll_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("voice_key_input_task");

    while (1) {
        watchdog_platform_feed_current_task();
#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
        if (s_io_expander != NULL) {
            uint32_t pin_levels = 0;
            esp_err_t ret = esp_io_expander_get_level(s_io_expander, VOICE_KEY_INPUT_ALL_MASK, &pin_levels);
            if (ret == ESP_OK) {
                if (!s_prev_input_valid) {
                    s_prev_input_levels = pin_levels;
                    s_prev_input_valid = true;
                    ESP_LOGI(TAG, "xl9555 initial input levels=0x%04" PRIx32, pin_levels & VOICE_KEY_INPUT_ALL_MASK);
                } else if (pin_levels != s_prev_input_levels) {
                    uint32_t changed_mask = (pin_levels ^ s_prev_input_levels) & VOICE_KEY_INPUT_ALL_MASK;
                    ESP_LOGI(
                        TAG,
                        "xl9555 input changed: old=0x%04" PRIx32 " new=0x%04" PRIx32 " changed=0x%04" PRIx32,
                        s_prev_input_levels & VOICE_KEY_INPUT_ALL_MASK,
                        pin_levels & VOICE_KEY_INPUT_ALL_MASK,
                        changed_mask);
                    s_prev_input_levels = pin_levels;
                }

                voice_key_input_handle_button_sample(
                    &s_expander_io4_state,
                    (pin_levels & VOICE_KEY_INPUT_KEY1_MASK_IO0_4) != 0);
                voice_key_input_handle_button_sample(
                    &s_expander_io5_state,
                    (pin_levels & VOICE_KEY_INPUT_KEY1_MASK_IO0_5) != 0);
            } else {
                ESP_LOGW(TAG, "key1 read failed: %s", esp_err_to_name(ret));
            }
        }
#endif

        voice_key_input_handle_button_sample(
            &s_direct_gpio_state,
            gpio_get_level(VOICE_KEY_INPUT_DIRECT_GPIO) != 0);

        vTaskDelay(pdMS_TO_TICKS(VOICE_KEY_INPUT_POLL_MS));
    }
}

esp_err_t voice_key_input_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_toggle_event_sem = xSemaphoreCreateCounting(VOICE_KEY_INPUT_EVENT_QUEUE_LENGTH, 0);
    ESP_RETURN_ON_FALSE(s_toggle_event_sem != NULL, ESP_ERR_NO_MEM, TAG, "voice key event queue create failed");
    s_recovery_event_sem = xSemaphoreCreateCounting(VOICE_KEY_INPUT_EVENT_QUEUE_LENGTH, 0);
    ESP_RETURN_ON_FALSE(s_recovery_event_sem != NULL, ESP_ERR_NO_MEM, TAG, "voice key recovery event queue create failed");

#if VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER
    esp_err_t expander_ret = voice_key_input_expander_init();
    if (expander_ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "voice key expander unavailable after retries, continuing with direct gpio fallback only: %s",
            esp_err_to_name(expander_ret));
        s_i2c_bus_handle = NULL;
        s_owns_i2c_bus = false;
        s_selected_bus_label = "direct_only";
        s_io_expander = NULL;
        s_expander_available = false;
    }
#endif
    ESP_RETURN_ON_ERROR(voice_key_input_direct_gpio_init(), TAG, "direct voice key init failed");

    BaseType_t task_ok = xTaskCreate(
        voice_key_input_poll_task,
        "voice_key_input_task",
        4096,
        NULL,
        5,
        &s_poll_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "voice key task create failed");

    s_started = true;
    ESP_LOGI(
        TAG,
        "voice key ready: source=%s gpio=%d active_low=1 legacy_expander=%d poll_ms=%d debounce_ms=%d debounce_samples=%d",
        VOICE_KEY_INPUT_DIRECT_LABEL,
        VOICE_KEY_INPUT_DIRECT_GPIO,
        VOICE_KEY_INPUT_ENABLE_LEGACY_EXPANDER,
        VOICE_KEY_INPUT_POLL_MS,
        VOICE_KEY_INPUT_DEBOUNCE_MS,
        VOICE_KEY_INPUT_DEBOUNCE_THRESHOLD);
    ESP_LOGI(
        TAG,
        "recording gesture key ready: single_click_toggle=1 double_click_recovery=1 double_click_window_ms=%d recovery_idle_guard_ms=%d long_press_reserved_ms=%d",
        VOICE_KEY_INPUT_DOUBLE_CLICK_WINDOW_MS,
        VOICE_KEY_INPUT_RECOVERY_IDLE_GUARD_MS,
        VOICE_KEY_INPUT_LONG_PRESS_IGNORE_MS);
    return ESP_OK;
}

bool voice_key_input_take_toggle_event(void)
{
    if (s_toggle_event_sem == NULL) {
        return false;
    }

    return xSemaphoreTake(s_toggle_event_sem, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool voice_key_input_take_recovery_event(void)
{
    if (s_recovery_event_sem == NULL) {
        return false;
    }

    return xSemaphoreTake(s_recovery_event_sem, 0) == pdTRUE;
}

const char *voice_key_input_get_active_source(void)
{
    return VOICE_KEY_INPUT_DIRECT_LABEL;
}

esp_err_t voice_key_input_set_recording_output(bool enabled)
{
    if (s_recording_output_enabled != enabled || !s_recording_output_change_seen) {
        s_recording_output_last_change_tick = xTaskGetTickCount();
        s_recording_output_change_seen = true;
    }
    s_recording_output_enabled = enabled;
    return ESP_OK;
}
