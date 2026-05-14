#include "voice_key_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"

#include "audio_capture_platform.h"

#define VOICE_KEY_INPUT_ALT_I2C_PORT   (1)
#define VOICE_KEY_INPUT_ALT_I2C_SDA_IO (38)
#define VOICE_KEY_INPUT_ALT_I2C_SCL_IO (48)
#define VOICE_KEY_INPUT_FALLBACK_SDA_IO (47)
#define VOICE_KEY_INPUT_FALLBACK_SCL_IO (48)
#define VOICE_KEY_INPUT_INT_IO         (46)
#define VOICE_KEY_INPUT_DIRECT_GPIO    (GPIO_NUM_0)
#define VOICE_KEY_INPUT_KEY1_MASK_IO0_4 (1U << 4)
#define VOICE_KEY_INPUT_KEY1_MASK_IO0_5 (1U << 5)
#define VOICE_KEY_INPUT_EXPANDER_KEY_MASKS (VOICE_KEY_INPUT_KEY1_MASK_IO0_4 | VOICE_KEY_INPUT_KEY1_MASK_IO0_5)
#define VOICE_KEY_INPUT_POLL_MS        (20)
#define VOICE_KEY_INPUT_I2C_TIMEOUT_MS (100)
#define VOICE_KEY_INPUT_ADDR           ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000
#define VOICE_KEY_INPUT_ALL_MASK       (0xFFFFU)

static const char *TAG = "voice_key_input";

typedef struct {
    const char *label;
    bool use_shared_audio_bus;
    i2c_port_num_t i2c_port;
    gpio_num_t sda_io;
    gpio_num_t scl_io;
} voice_key_input_bus_candidate_t;

static bool s_started;
static i2c_master_bus_handle_t s_i2c_bus_handle;
static esp_io_expander_handle_t s_io_expander;
static TaskHandle_t s_poll_task_handle;
static bool s_prev_pressed;
static bool s_prev_raw_high;
static bool s_idle_level_high;
static bool s_idle_level_valid;
static bool s_alt_prev_pressed;
static bool s_alt_prev_raw_high;
static bool s_alt_idle_level_high;
static bool s_alt_idle_level_valid;
static bool s_prev_input_valid;
static uint32_t s_prev_input_levels;
static bool s_direct_prev_pressed;
static bool s_direct_prev_raw_high;
static bool s_direct_idle_level_high;
static bool s_direct_idle_level_valid;
static volatile uint32_t s_toggle_event_count;
static bool s_owns_i2c_bus;
static const char *s_selected_bus_label;

static void voice_key_input_record_toggle_event(const char *source)
{
    s_toggle_event_count++;
    ESP_LOGI(TAG, "%s press edge detected, toggle_event_count=%" PRIu32, source, s_toggle_event_count);
}

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
            "xl9555 candidate selected: %s sda=%d scl=%d shared=%s",
            candidate->label,
            candidate->sda_io,
            candidate->scl_io,
            owns_bus ? "no" : "yes");
        break;
    }

    ESP_RETURN_ON_FALSE(s_i2c_bus_handle != NULL, last_err, TAG, "no xl9555 candidate bus responded");

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
    return ESP_OK;
}

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

    while (1) {
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

            bool raw_high = (pin_levels & VOICE_KEY_INPUT_KEY1_MASK_IO0_4) != 0;
            if (!s_idle_level_valid) {
                s_idle_level_high = raw_high;
                s_prev_raw_high = raw_high;
                s_idle_level_valid = true;
                ESP_LOGI(
                    TAG,
                    "voice key candidate idle level detected: source=xl9555.io0_4 raw_high=%d pressed_when=%s",
                    raw_high ? 1 : 0,
                    raw_high ? "low" : "high");
            } else if (raw_high != s_prev_raw_high) {
                s_prev_raw_high = raw_high;
                ESP_LOGI(TAG, "voice key candidate level changed: source=xl9555.io0_4 raw_high=%d", raw_high ? 1 : 0);
            }

            bool pressed = raw_high != s_idle_level_high;
            if (pressed && !s_prev_pressed) {
                voice_key_input_record_toggle_event("xl9555.io0_4");
            }
            s_prev_pressed = pressed;

            bool alt_raw_high = (pin_levels & VOICE_KEY_INPUT_KEY1_MASK_IO0_5) != 0;
            if (!s_alt_idle_level_valid) {
                s_alt_idle_level_high = alt_raw_high;
                s_alt_prev_raw_high = alt_raw_high;
                s_alt_idle_level_valid = true;
                ESP_LOGI(
                    TAG,
                    "voice key candidate idle level detected: source=xl9555.io0_5 raw_high=%d pressed_when=%s",
                    alt_raw_high ? 1 : 0,
                    alt_raw_high ? "low" : "high");
            } else if (alt_raw_high != s_alt_prev_raw_high) {
                s_alt_prev_raw_high = alt_raw_high;
                ESP_LOGI(TAG, "voice key candidate level changed: source=xl9555.io0_5 raw_high=%d", alt_raw_high ? 1 : 0);
            }

            bool alt_pressed = alt_raw_high != s_alt_idle_level_high;
            if (alt_pressed && !s_alt_prev_pressed) {
                voice_key_input_record_toggle_event("xl9555.io0_5");
            }
            s_alt_prev_pressed = alt_pressed;
        } else {
            ESP_LOGW(TAG, "key1 read failed: %s", esp_err_to_name(ret));
        }

        int direct_level = gpio_get_level(VOICE_KEY_INPUT_DIRECT_GPIO);
        bool direct_raw_high = direct_level != 0;
        if (!s_direct_idle_level_valid) {
            s_direct_idle_level_high = direct_raw_high;
            s_direct_prev_raw_high = direct_raw_high;
            s_direct_idle_level_valid = true;
            ESP_LOGI(
                TAG,
                "direct key idle level detected: gpio=%d raw_high=%d pressed_when=%s",
                VOICE_KEY_INPUT_DIRECT_GPIO,
                direct_raw_high ? 1 : 0,
                direct_raw_high ? "low" : "high");
        } else if (direct_raw_high != s_direct_prev_raw_high) {
            s_direct_prev_raw_high = direct_raw_high;
            ESP_LOGI(
                TAG,
                "direct key level changed: gpio=%d raw_high=%d",
                VOICE_KEY_INPUT_DIRECT_GPIO,
                direct_raw_high ? 1 : 0);
        }

        bool direct_pressed = direct_raw_high != s_direct_idle_level_high;
        if (direct_pressed && !s_direct_prev_pressed) {
            voice_key_input_record_toggle_event("direct.gpio0");
        }
        s_direct_prev_pressed = direct_pressed;

        vTaskDelay(pdMS_TO_TICKS(VOICE_KEY_INPUT_POLL_MS));
    }
}

esp_err_t voice_key_input_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(voice_key_input_expander_init(), TAG, "voice key expander init failed");
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
        "voice key ready: expander_candidates=xl9555.io0_4/xl9555.io0_5 + direct.gpio0 bus=%s int_gpio=%d",
        s_selected_bus_label != NULL ? s_selected_bus_label : "unknown",
        VOICE_KEY_INPUT_INT_IO);
    return ESP_OK;
}

bool voice_key_input_take_toggle_event(void)
{
    if (s_toggle_event_count == 0) {
        return false;
    }

    s_toggle_event_count--;
    return true;
}

esp_err_t voice_key_input_set_recording_output(bool enabled)
{
    (void)enabled;
    return ESP_OK;
}
