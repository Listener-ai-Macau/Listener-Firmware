#include "keyboard.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_hid.h"
#include "board_pins.h"
#include "diag_log.h"
#include "hid_keyboard.h"
#include "power_manager.h"
#include "voice_recording_control.h"
#include "watchdog_platform.h"

#define KEYBOARD_WASD_POLL_MS 20
#define KEYBOARD_WASD_DEBOUNCE_SAMPLES 8
#define KEYBOARD_EC11_POLL_MS KEYBOARD_WASD_POLL_MS
#define KEYBOARD_EC11_DEBOUNCE_SAMPLES 2

typedef struct {
    gpio_num_t gpio;
    char output_char;
    const char *label;
    bool initialized;
    bool last_sample_high;
    bool stable_level_high;
    uint8_t stable_count;
    bool pressed;
} keyboard_wasd_key_t;

typedef struct {
    gpio_num_t a_gpio;
    gpio_num_t b_gpio;
    bool initialized;
    uint8_t last_state;
    uint8_t stable_state;
    uint8_t stable_count;
    int32_t detent_accumulator;
    uint32_t clockwise_count;
    uint32_t counter_clockwise_count;
} keyboard_ec11_state_t;

static const char *TAG = "keyboard";
static TaskHandle_t s_wasd_task_handle;
static TaskHandle_t s_ec11_task_handle;
static keyboard_wasd_key_t s_wasd_keys[] = {
    {
        .gpio = BOARD_PINS_KEY2_IO,
        .output_char = 'w',
        .label = "key2.gpio48.w",
    },
    {
        .gpio = BOARD_PINS_KEY3_IO,
        .output_char = 'a',
        .label = "key3.gpio47.a",
    },
    {
        .gpio = BOARD_PINS_KEY4_IO,
        .output_char = 's',
        .label = "key4.gpio21.s",
    },
};
static keyboard_ec11_state_t s_ec11_state = {
    .a_gpio = BOARD_PINS_EC11_A_IO,
    .b_gpio = BOARD_PINS_EC11_B_IO,
};

static int8_t keyboard_ec11_quadrature_delta(uint8_t previous, uint8_t current)
{
    switch ((previous << 2) | current) {
    case 0x01:
    case 0x07:
    case 0x0E:
    case 0x08:
        return 1;
    case 0x02:
    case 0x0B:
    case 0x0D:
    case 0x04:
        return -1;
    default:
        return 0;
    }
}

static void keyboard_wasd_handle_sample(keyboard_wasd_key_t *key, bool raw_high)
{
    if (!key->initialized) {
        key->initialized = true;
        key->last_sample_high = raw_high;
        key->stable_level_high = raw_high;
        key->stable_count = 1;
        key->pressed = false;
        ESP_LOGI(TAG, "WASD key idle detected: source=%s raw_high=%d", key->label, raw_high ? 1 : 0);
        return;
    }

    if (raw_high == key->last_sample_high) {
        if (key->stable_count < UINT8_MAX) {
            key->stable_count++;
        }
    } else {
        ESP_LOGI(
            TAG,
            "WASD key raw transition: source=%s raw_high=%d stable_high=%d",
            key->label,
            raw_high ? 1 : 0,
            key->stable_level_high ? 1 : 0);
        key->last_sample_high = raw_high;
        key->stable_count = 1;
        return;
    }

    if (key->stable_count < KEYBOARD_WASD_DEBOUNCE_SAMPLES || raw_high == key->stable_level_high) {
        return;
    }

    key->stable_level_high = raw_high;
    bool pressed = !raw_high;
    power_manager_record_activity(key->label);
    ESP_LOGI(
        TAG,
        "WASD key stable transition: source=%s raw_high=%d pressed=%d",
        key->label,
        raw_high ? 1 : 0,
        pressed ? 1 : 0);
    if (pressed && !key->pressed) {
        esp_err_t ret = ble_hid_send_ascii_async(key->output_char);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WASD key press queued: source=%s output=%c", key->label, key->output_char);
        } else {
            ESP_LOGW(
                TAG,
                "WASD key press dropped: source=%s output=%c error=%s",
                key->label,
                key->output_char,
                esp_err_to_name(ret));
        }
    }
    key->pressed = pressed;
}

static void keyboard_wasd_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("keyboard_wasd_task");

    while (1) {
        watchdog_platform_feed_current_task();
        for (size_t index = 0; index < sizeof(s_wasd_keys) / sizeof(s_wasd_keys[0]); ++index) {
            int level = gpio_get_level(s_wasd_keys[index].gpio);
            keyboard_wasd_handle_sample(&s_wasd_keys[index], level != 0);
        }
        vTaskDelay(pdMS_TO_TICKS(KEYBOARD_WASD_POLL_MS));
    }
}

static void keyboard_ec11_handle_sample(keyboard_ec11_state_t *state, uint8_t raw_state)
{
    if (!state->initialized) {
        state->initialized = true;
        state->last_state = raw_state;
        state->stable_state = raw_state;
        state->stable_count = 1;
        ESP_LOGI(
            TAG,
            "EC11 idle detected: a_gpio=%d b_gpio=%d state=0x%02x active_low=1",
            (int)state->a_gpio,
            (int)state->b_gpio,
            raw_state);
        return;
    }

    if (raw_state == state->last_state) {
        if (state->stable_count < UINT8_MAX) {
            state->stable_count++;
        }
    } else {
        state->last_state = raw_state;
        state->stable_count = 1;
        return;
    }

    if (state->stable_count < KEYBOARD_EC11_DEBOUNCE_SAMPLES ||
        raw_state == state->stable_state) {
        return;
    }

    int8_t delta = keyboard_ec11_quadrature_delta(state->stable_state, raw_state);
    state->stable_state = raw_state;
    if (delta == 0) {
        ESP_LOGI(TAG, "EC11 ignored invalid transition: state=0x%02x", raw_state);
        return;
    }

    state->detent_accumulator += delta;
    power_manager_record_activity("ec11_rotate");
    ESP_LOGI(
        TAG,
        "EC11 transition: state=0x%02x delta=%d accumulator=%" PRId32,
        raw_state,
        (int)delta,
        state->detent_accumulator);

    if (state->detent_accumulator >= 4) {
        state->detent_accumulator = 0;
        state->clockwise_count++;
        ESP_LOGI(TAG, "EC11 detent: direction=clockwise count=%" PRIu32, state->clockwise_count);
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_EC11_DETENT, DIAG_SEV_INFO,
                 1, state->clockwise_count, 0, 0);
    } else if (state->detent_accumulator <= -4) {
        state->detent_accumulator = 0;
        state->counter_clockwise_count++;
        ESP_LOGI(TAG, "EC11 detent: direction=counter_clockwise count=%" PRIu32,
                 state->counter_clockwise_count);
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_EC11_DETENT, DIAG_SEV_INFO,
                 2, state->counter_clockwise_count, 0, 0);
    }
}

static void keyboard_ec11_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("keyboard_ec11_task");

    while (1) {
        watchdog_platform_feed_current_task();
        uint8_t raw_state =
            (gpio_get_level(s_ec11_state.a_gpio) ? 0x01u : 0x00u) |
            (gpio_get_level(s_ec11_state.b_gpio) ? 0x02u : 0x00u);
        keyboard_ec11_handle_sample(&s_ec11_state, raw_state);
        vTaskDelay(pdMS_TO_TICKS(KEYBOARD_EC11_POLL_MS));
    }
}

static esp_err_t keyboard_wasd_start(void)
{
    if (s_wasd_task_handle != NULL) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_PINS_KEY2_IO) |
                        (1ULL << BOARD_PINS_KEY3_IO) |
                        (1ULL << BOARD_PINS_KEY4_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WASD GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ok = xTaskCreate(
        keyboard_wasd_task,
        "keyboard_wasd_task",
        3072,
        NULL,
        4,
        &s_wasd_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "WASD task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "WASD keys ready: key1=gpio45:d key2=gpio48:w key3=gpio47:a key4=gpio21:s active_low=1 poll_ms=%d debounce_samples=%d",
        KEYBOARD_WASD_POLL_MS,
        KEYBOARD_WASD_DEBOUNCE_SAMPLES);
    return ESP_OK;
}

static esp_err_t keyboard_ec11_start(void)
{
    if (s_ec11_task_handle != NULL) {
        return ESP_OK;
    }
    if (BOARD_PINS_EC11_A_IO == GPIO_NUM_NC || BOARD_PINS_EC11_B_IO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "EC11 A/B disabled: gpio missing");
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_PINS_EC11_A_IO) |
                        (1ULL << BOARD_PINS_EC11_B_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 GPIO config failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_GPIO_FAIL, DIAG_SEV_ERROR, 2, ret, 0, 0);
        return ret;
    }

    BaseType_t task_ok = xTaskCreate(
        keyboard_ec11_task,
        "keyboard_ec11_task",
        3072,
        NULL,
        4,
        &s_ec11_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "EC11 task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "EC11 ready: a=gpio36 b=gpio38 key=gpio35 key_policy=gpio35_no_deep_sleep_wake poll_ms=%d debounce_samples=%d",
        KEYBOARD_EC11_POLL_MS,
        KEYBOARD_EC11_DEBOUNCE_SAMPLES);
    return ESP_OK;
}

esp_err_t keyboard_start(void)
{
    hid_keyboard_init();

    esp_err_t voice_ret = voice_recording_control_start();
    if (voice_ret != ESP_OK) {
        ESP_LOGW(TAG, "voice recording control started degraded: %s", esp_err_to_name(voice_ret));
    }

    esp_err_t wasd_ret = keyboard_wasd_start();
    if (wasd_ret != ESP_OK) {
        return wasd_ret;
    }

    esp_err_t ec11_ret = keyboard_ec11_start();
    if (ec11_ret != ESP_OK && ec11_ret != ESP_ERR_NOT_SUPPORTED) {
        return ec11_ret;
    }

    if (voice_ret != ESP_OK) {
        return voice_ret;
    }
    return ESP_OK;
}

esp_err_t keyboard_start_safe_mode(void)
{
    hid_keyboard_init();
    ESP_LOGW(TAG, "safe mode: voice recording control and audio capture are disabled");
    esp_err_t wasd_ret = keyboard_wasd_start();
    if (wasd_ret != ESP_OK) {
        return wasd_ret;
    }
    esp_err_t ec11_ret = keyboard_ec11_start();
    return ec11_ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ec11_ret;
}

uint32_t keyboard_get_key_press_count(void)
{
    return hid_keyboard_get_key_press_count();
}
