#include "keyboard.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ble_hid.h"
#include "ble_audio_stream.h"
#include "board_pins.h"
#include "diag_log.h"
#include "device_settings.h"
#include "ec11_rotation_control.h"
#include "hid_keyboard.h"
#include "power_manager.h"
#include "status_led.h"
#include "voice_key_input.h"
#include "voice_recording_control.h"
#include "watchdog_platform.h"

#define KEYBOARD_CUSTOM_POLL_MS 10
#define KEYBOARD_CUSTOM_IDLE_BACKUP_POLL_MS 20
#define KEYBOARD_CUSTOM_LOW_POWER_IDLE_BACKUP_POLL_MS 20
#define KEYBOARD_CUSTOM_DEBOUNCE_MS 20
#define KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES \
    ((KEYBOARD_CUSTOM_DEBOUNCE_MS + KEYBOARD_CUSTOM_POLL_MS - 1) / KEYBOARD_CUSTOM_POLL_MS)
#define KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS 200
#define KEYBOARD_CUSTOM_LONG_PRESS_MS 1000
#define KEYBOARD_CUSTOM_GENERATED_PRESS_MS 160
#define KEYBOARD_CUSTOM_GENERATED_RELEASE_SETTLE_MS 80
#define KEYBOARD_CUSTOM_GENERATED_EVENT_QUEUE_DEPTH 8
#define KEYBOARD_EC11_IDLE_POLL_MS 20
#define KEYBOARD_EC11_LOW_POWER_IDLE_POLL_MS 20
#define KEYBOARD_EC11_EVENT_QUEUE_DEPTH 256
#define KEYBOARD_EC11_DETENT_STATE 0x03u
#define KEYBOARD_EC11_FEEDBACK_EDGE_REFRESH_MS 60
#define KEYBOARD_EC11_DROP_LOG_INTERVAL_MS 1000

#define KEYBOARD_CUSTOM_PHASE_PRESS 1u
#define KEYBOARD_CUSTOM_PHASE_RELEASE 2u
#define KEYBOARD_CUSTOM_PHASE_SINGLE 3u
#define KEYBOARD_CUSTOM_PHASE_DOUBLE 4u
#define KEYBOARD_CUSTOM_PHASE_LONG 5u

#define KEYBOARD_INPUT_DEBUG_KEY_RAW 1u
#define KEYBOARD_INPUT_DEBUG_KEY_STABLE 2u
#define KEYBOARD_INPUT_DEBUG_EC11_TRANSITION 3u
#define KEYBOARD_INPUT_DEBUG_EC11_INVALID 4u
#define KEYBOARD_INPUT_DEBUG_EC11_PARTIAL 5u
#define KEYBOARD_INPUT_DEBUG_EC11_DISPATCH 6u

#define KEYBOARD_EC11_DELTA_POSITIVE 1u
#define KEYBOARD_EC11_DELTA_NEGATIVE 2u
#define KEYBOARD_EC11_ACTION_VOLUME 1u
#define KEYBOARD_EC11_ACTION_BRIGHTNESS 2u
#define KEYBOARD_EC11_ACTION_DISABLED 3u

typedef enum {
    KEYBOARD_CUSTOM_GESTURE_SINGLE = 0,
    KEYBOARD_CUSTOM_GESTURE_DOUBLE,
    KEYBOARD_CUSTOM_GESTURE_LONG,
} keyboard_custom_gesture_t;

typedef struct {
    gpio_num_t gpio;
    uint8_t logical_key;
    uint8_t single_usage;
    uint8_t double_usage;
    uint8_t long_usage;
    const char *logical_name;
    const char *label;
    const char *source_base;
    uint8_t index;
    bool initialized;
    bool last_sample_high;
    bool stable_level_high;
    uint8_t stable_count;
    bool pressed;
    bool long_sent;
    bool pending_single;
    bool double_candidate;
    bool raw_feedback_pressed;
    TickType_t press_tick;
    TickType_t pending_single_due_tick;
} keyboard_custom_key_t;

typedef struct {
    gpio_num_t a_gpio;
    gpio_num_t b_gpio;
    bool initialized;
    uint8_t last_state;
    int32_t detent_accumulator;
    uint32_t clockwise_count;
    uint32_t counter_clockwise_count;
    uint32_t invalid_transition_count;
    uint32_t isr_drop_count;
    TickType_t last_feedback_tick;
    TickType_t last_drop_log_tick;
    int8_t last_feedback_delta;
} keyboard_ec11_state_t;

typedef struct {
    uint8_t raw_state;
} keyboard_ec11_event_t;

typedef struct {
    uint8_t logical_key;
    keyboard_custom_gesture_t gesture;
} keyboard_custom_generated_event_t;

typedef struct {
    bool active;
    keyboard_custom_gesture_t gesture;
    TickType_t started_tick;
} keyboard_custom_generated_state_t;

static const char *TAG = "keyboard";
static TaskHandle_t s_custom_task_handle;
static TaskHandle_t s_ec11_task_handle;
static QueueHandle_t s_ec11_event_queue;
static QueueHandle_t s_custom_generated_event_queue;
static volatile bool s_ec11_isr_last_raw_valid;
static volatile uint8_t s_ec11_isr_last_raw_state;
static volatile bool s_ec11_overflow_pending;
static volatile uint8_t s_ec11_overflow_raw_state;
static keyboard_custom_key_t s_custom_keys[] = {
    {
        .gpio = BOARD_PINS_KEY1_IO,
        .logical_key = 1,
        .single_usage = HID_KEYBOARD_USAGE_F13,
        .double_usage = HID_KEYBOARD_USAGE_F17,
        .long_usage = HID_KEYBOARD_USAGE_F21,
        .logical_name = "KEY1",
        .label = "key1.gpio38.f13",
        .source_base = "key1.gpio38",
        .index = 0,
    },
    {
        .gpio = BOARD_PINS_KEY2_IO,
        .logical_key = 2,
        .single_usage = HID_KEYBOARD_USAGE_F14,
        .double_usage = HID_KEYBOARD_USAGE_F18,
        .long_usage = HID_KEYBOARD_USAGE_F22,
        .logical_name = "KEY2",
        .label = "key2.gpio39.f14",
        .source_base = "key2.gpio39",
        .index = 1,
    },
    {
        .gpio = BOARD_PINS_KEY3_IO,
        .logical_key = 3,
        .single_usage = HID_KEYBOARD_USAGE_F15,
        .double_usage = HID_KEYBOARD_USAGE_F19,
        .long_usage = HID_KEYBOARD_USAGE_F23,
        .logical_name = "KEY3",
        .label = "key3.gpio40.f15",
        .source_base = "key3.gpio40",
        .index = 2,
    },
    {
        .gpio = BOARD_PINS_KEY4_IO,
        .logical_key = 4,
        .single_usage = HID_KEYBOARD_USAGE_F16,
        .double_usage = HID_KEYBOARD_USAGE_F20,
        .long_usage = HID_KEYBOARD_USAGE_F24,
        .logical_name = "KEY4",
        .label = "key4.gpio41.f16",
        .source_base = "key4.gpio41",
        .index = 3,
    },
};
static keyboard_custom_generated_state_t s_custom_generated_states[
    sizeof(s_custom_keys) / sizeof(s_custom_keys[0])
];
static keyboard_ec11_state_t s_ec11_state = {
    .a_gpio = BOARD_PINS_EC11_A_IO,
    .b_gpio = BOARD_PINS_EC11_B_IO,
};

static esp_err_t keyboard_ec11_dispatch_rotation(
    ec11_rotation_direction_t direction,
    const char *source);
static void keyboard_custom_wake_task(void);

static void keyboard_enable_active_low_light_sleep_wake(gpio_num_t gpio, const char *label)
{
    if (gpio == GPIO_NUM_NC) {
        return;
    }

    esp_err_t ret = gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL);
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "GPIO light-sleep wake enable failed: label=%s gpio=%d ret=%s",
            label != NULL ? label : "unknown",
            (int)gpio,
            esp_err_to_name(ret));
        return;
    }

    ret = esp_sleep_enable_gpio_wakeup();
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "GPIO light-sleep wake source enable failed: label=%s gpio=%d ret=%s",
            label != NULL ? label : "unknown",
            (int)gpio,
            esp_err_to_name(ret));
    }
}

static int8_t keyboard_ec11_quadrature_delta(uint8_t previous, uint8_t current)
{
    /* V2 A/B phase order is mapped so physical clockwise is the logical increase direction. */
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

static uint8_t keyboard_ec11_read_raw_state(void)
{
    return (gpio_get_level(s_ec11_state.a_gpio) ? 0x01u : 0x00u) |
           (gpio_get_level(s_ec11_state.b_gpio) ? 0x02u : 0x00u);
}

static void keyboard_custom_log_event(
    const keyboard_custom_key_t *key,
    uint32_t phase,
    uint8_t usage,
    esp_err_t result)
{
    diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_CUSTOM_KEY, DIAG_SEV_INFO,
             key->logical_key,
             phase,
             usage,
             (uint32_t)result);
}

static void keyboard_input_debug_log(uint32_t kind, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    if (!diag_log_input_debug_enabled()) {
        return;
    }

    diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_INPUT_DEBUG, DIAG_SEV_INFO,
             kind, arg2, arg3, arg4);
}

static uint32_t keyboard_ec11_delta_code(int8_t delta)
{
    if (delta > 0) {
        return KEYBOARD_EC11_DELTA_POSITIVE;
    }
    if (delta < 0) {
        return KEYBOARD_EC11_DELTA_NEGATIVE;
    }
    return 0;
}

static int8_t keyboard_ec11_feedback_delta_from_accumulator(int32_t accumulator)
{
    if (accumulator > 0) {
        return 1;
    }
    if (accumulator < 0) {
        return -1;
    }
    return 0;
}

static uint32_t keyboard_ec11_action_code(ec11_rotation_action_t action)
{
    switch (action) {
    case EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS:
        return KEYBOARD_EC11_ACTION_BRIGHTNESS;
    case EC11_ROTATION_ACTION_DISABLED:
        return KEYBOARD_EC11_ACTION_DISABLED;
    case EC11_ROTATION_ACTION_SYSTEM_VOLUME:
    default:
        return KEYBOARD_EC11_ACTION_VOLUME;
    }
}

static void keyboard_ec11_refresh_feedback_for_delta(
    keyboard_ec11_state_t *state,
    int8_t delta,
    bool force)
{
    if (delta == 0) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t refresh_ticks = pdMS_TO_TICKS(KEYBOARD_EC11_FEEDBACK_EDGE_REFRESH_MS);
    if (refresh_ticks == 0) {
        refresh_ticks = 1;
    }

    bool direction_changed =
        state->last_feedback_delta != 0 &&
        ((state->last_feedback_delta > 0) != (delta > 0));
    if (!force &&
        state->last_feedback_tick != 0 &&
        !direction_changed &&
        (now - state->last_feedback_tick) < refresh_ticks) {
        return;
    }

    status_led_refresh_ec11_feedback(delta > 0
        ? STATUS_LED_EC11_FEEDBACK_ROTATE_CW
        : STATUS_LED_EC11_FEEDBACK_ROTATE_CCW);
    state->last_feedback_tick = now;
    state->last_feedback_delta = delta;
}

static uint32_t keyboard_custom_elapsed_ms(TickType_t now, TickType_t start)
{
    uint64_t elapsed_ms = (uint64_t)(now - start) * (uint64_t)portTICK_PERIOD_MS;
    return elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
}

static uint32_t keyboard_custom_ticks_until_ms(TickType_t now, TickType_t deadline)
{
    int32_t remaining_ticks = (int32_t)(deadline - now);
    if (remaining_ticks <= 0) {
        return 0;
    }
    uint64_t remaining_ms = (uint64_t)remaining_ticks * (uint64_t)portTICK_PERIOD_MS;
    return remaining_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining_ms;
}

static bool keyboard_custom_tick_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool keyboard_power_state_is_low_power_idle(void)
{
    power_manager_state_t state = power_manager_get_state();
    return state == POWER_MANAGER_STATE_CONNECTED_IDLE ||
           state == POWER_MANAGER_STATE_DISCONNECTED_IDLE ||
           state == POWER_MANAGER_STATE_HARDWARE_SHUTDOWN;
}

static keyboard_custom_key_t *keyboard_custom_find_key(uint8_t logical_key)
{
    for (size_t index = 0; index < sizeof(s_custom_keys) / sizeof(s_custom_keys[0]); ++index) {
        if (s_custom_keys[index].logical_key == logical_key) {
            return &s_custom_keys[index];
        }
    }
    return NULL;
}

static uint8_t keyboard_custom_usage_for_gesture(
    const keyboard_custom_key_t *key,
    keyboard_custom_gesture_t gesture)
{
    switch (gesture) {
    case KEYBOARD_CUSTOM_GESTURE_DOUBLE:
        return key->double_usage;
    case KEYBOARD_CUSTOM_GESTURE_LONG:
        return key->long_usage;
    case KEYBOARD_CUSTOM_GESTURE_SINGLE:
    default:
        return key->single_usage;
    }
}

static uint32_t keyboard_custom_phase_for_gesture(keyboard_custom_gesture_t gesture)
{
    switch (gesture) {
    case KEYBOARD_CUSTOM_GESTURE_DOUBLE:
        return KEYBOARD_CUSTOM_PHASE_DOUBLE;
    case KEYBOARD_CUSTOM_GESTURE_LONG:
        return KEYBOARD_CUSTOM_PHASE_LONG;
    case KEYBOARD_CUSTOM_GESTURE_SINGLE:
    default:
        return KEYBOARD_CUSTOM_PHASE_SINGLE;
    }
}

static const char *keyboard_custom_gesture_name(keyboard_custom_gesture_t gesture)
{
    switch (gesture) {
    case KEYBOARD_CUSTOM_GESTURE_DOUBLE:
        return "double";
    case KEYBOARD_CUSTOM_GESTURE_LONG:
        return "long";
    case KEYBOARD_CUSTOM_GESTURE_SINGLE:
    default:
        return "single";
    }
}

static status_led_key_feedback_t keyboard_custom_led_feedback_for_gesture(
    keyboard_custom_gesture_t gesture)
{
    switch (gesture) {
    case KEYBOARD_CUSTOM_GESTURE_DOUBLE:
        return STATUS_LED_KEY_FEEDBACK_DOUBLE;
    case KEYBOARD_CUSTOM_GESTURE_LONG:
        return STATUS_LED_KEY_FEEDBACK_LONG;
    case KEYBOARD_CUSTOM_GESTURE_SINGLE:
    default:
        return STATUS_LED_KEY_FEEDBACK_SINGLE;
    }
}

static unsigned int keyboard_custom_function_number(uint8_t usage)
{
    if (usage >= HID_KEYBOARD_USAGE_F13 && usage <= HID_KEYBOARD_USAGE_F24) {
        return 13u + (unsigned int)(usage - HID_KEYBOARD_USAGE_F13);
    }
    return usage;
}

static void keyboard_custom_make_source_label(
    const keyboard_custom_key_t *key,
    uint8_t usage,
    char *buffer,
    size_t buffer_size)
{
    unsigned int function_number = keyboard_custom_function_number(usage);
    if (usage == key->single_usage) {
        snprintf(buffer, buffer_size, "%s", key->label);
    } else {
        snprintf(buffer, buffer_size, "%s.f%u", key->source_base, function_number);
    }
}

static void keyboard_custom_send_gesture(
    keyboard_custom_key_t *key,
    keyboard_custom_gesture_t gesture)
{
    uint8_t usage = keyboard_custom_usage_for_gesture(key, gesture);
    unsigned int function_number = keyboard_custom_function_number(usage);
    char source_label[32];
    keyboard_custom_make_source_label(key, usage, source_label, sizeof(source_label));

    status_led_notify_key_feedback(key->index, keyboard_custom_led_feedback_for_gesture(gesture));
    esp_err_t ret = ble_hid_send_keyboard_usage_async(usage, source_label);
    keyboard_custom_log_event(key, keyboard_custom_phase_for_gesture(gesture), usage, ret);
    if (ret == ESP_OK) {
        if (gesture == KEYBOARD_CUSTOM_GESTURE_SINGLE) {
            ESP_LOGI(
                TAG,
                "custom key fallback queued: logical=%s source=%s usage=F%u gesture=single",
                key->logical_name,
                source_label,
                function_number);
        } else {
            ESP_LOGI(
                TAG,
                "custom key gesture queued: logical=%s source=%s usage=F%u gesture=%s",
                key->logical_name,
                source_label,
                function_number,
                keyboard_custom_gesture_name(gesture));
        }
    } else {
        if (ret != ESP_ERR_INVALID_STATE) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_RETRYABLE, "hid_key_send_failed");
        }
        ESP_LOGW(
            TAG,
            "custom key gesture dropped: logical=%s source=%s usage=0x%02X gesture=%s error=%s",
            key->logical_name,
            source_label,
            usage,
            keyboard_custom_gesture_name(gesture),
            esp_err_to_name(ret));
    }
}

static esp_err_t keyboard_custom_enqueue_generated_gesture(
    uint8_t logical_key,
    keyboard_custom_gesture_t gesture)
{
    keyboard_custom_key_t *key = keyboard_custom_find_key(logical_key);
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_custom_generated_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    keyboard_custom_generated_event_t event = {
        .logical_key = logical_key,
        .gesture = gesture,
    };
    if (xQueueSend(s_custom_generated_event_queue, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    keyboard_custom_wake_task();
    power_manager_record_activity("generated_custom_key");
    ESP_LOGI(
        TAG,
        "custom key generated gesture queued: logical=%s source=%s gesture=%s press_ms=%d double_ms=%d long_ms=%d",
        key->logical_name,
        key->label,
        keyboard_custom_gesture_name(gesture),
        KEYBOARD_CUSTOM_GENERATED_PRESS_MS,
        KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS,
        KEYBOARD_CUSTOM_LONG_PRESS_MS);
    return ESP_OK;
}

static bool keyboard_custom_parse_generated_command(
    const char *command,
    uint8_t *logical_key,
    keyboard_custom_gesture_t *gesture)
{
    if (command == NULL || logical_key == NULL || gesture == NULL) {
        return false;
    }

    const char *gesture_text = NULL;
    if (strncmp(command, "KEY", strlen("KEY")) == 0 &&
        command[3] >= '1' && command[3] <= '4' &&
        command[4] == ':') {
        *logical_key = (uint8_t)(command[3] - '0');
        gesture_text = command + 5;
    } else if (command[0] >= '1' && command[0] <= '4' && command[1] == ':') {
        *logical_key = (uint8_t)(command[0] - '0');
        gesture_text = command + 2;
    } else {
        return false;
    }

    if (strcmp(gesture_text, "SINGLE") == 0) {
        *gesture = KEYBOARD_CUSTOM_GESTURE_SINGLE;
        return true;
    }
    if (strcmp(gesture_text, "DOUBLE") == 0) {
        *gesture = KEYBOARD_CUSTOM_GESTURE_DOUBLE;
        return true;
    }
    if (strcmp(gesture_text, "LONG") == 0) {
        *gesture = KEYBOARD_CUSTOM_GESTURE_LONG;
        return true;
    }
    return false;
}

static bool keyboard_consume_usb_command(const char *line, esp_err_t *out_ret)
{
    if (line == NULL || out_ret == NULL) {
        return false;
    }

    const char *command = NULL;
    if (strncmp(line, "~KEY:", strlen("~KEY:")) == 0) {
        command = line + strlen("~KEY:");
    } else if (strncmp(line, "KEY:", strlen("KEY:")) == 0) {
        command = line + strlen("KEY:");
    }
    if (command == NULL) {
        return false;
    }

    power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, true);
    uint8_t logical_key = 0;
    keyboard_custom_gesture_t gesture = KEYBOARD_CUSTOM_GESTURE_SINGLE;
    if (keyboard_custom_parse_generated_command(command, &logical_key, &gesture)) {
        keyboard_custom_key_t *key = keyboard_custom_find_key(logical_key);
        *out_ret = keyboard_custom_enqueue_generated_gesture(logical_key, gesture);
        printf(
            "~KEY:GENERATED logical=%s gesture=%s result=%s\n",
            key != NULL ? key->logical_name : "UNKNOWN",
            keyboard_custom_gesture_name(gesture),
            esp_err_to_name(*out_ret));
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
        return true;
    }
    if (strcmp(command, "EC11:SINGLE") == 0 || strcmp(command, "VOICE:SINGLE") == 0) {
        *out_ret = voice_key_input_enqueue_generated_single_click();
        printf(
            "~KEY:GENERATED logical=EC11 gesture=single result=%s\n",
            esp_err_to_name(*out_ret));
        power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
        return true;
    }

    *out_ret = ESP_ERR_INVALID_ARG;
    printf("~KEY:GENERATED command=%s result=%s\n", command, esp_err_to_name(*out_ret));
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_USB_COMMAND, false);
    return true;
}

static void keyboard_custom_cancel_pending_single(keyboard_custom_key_t *key)
{
    key->pending_single = false;
    key->double_candidate = false;
}

static void keyboard_custom_wake_task(void)
{
    if (s_custom_task_handle != NULL) {
        xTaskNotifyGive(s_custom_task_handle);
    }
}

static void keyboard_custom_apply_raw_feedback(
    keyboard_custom_key_t *key,
    TickType_t now,
    const char *origin)
{
    (void)now;
    if (key == NULL || key->raw_feedback_pressed) {
        return;
    }
    key->raw_feedback_pressed = true;
    power_manager_record_activity(key->logical_name);
    status_led_notify_key_event(key->index, true);
    ESP_LOGI(
        TAG,
        "custom key raw press feedback: logical=%s source=%s origin=%s",
        key->logical_name,
        key->label,
        origin != NULL ? origin : "raw");
    keyboard_input_debug_log(
        KEYBOARD_INPUT_DEBUG_KEY_RAW,
        key->logical_key,
        0u,
        0u);
}

static void keyboard_custom_clear_raw_feedback(keyboard_custom_key_t *key)
{
    if (key == NULL || !key->raw_feedback_pressed) {
        return;
    }
    key->raw_feedback_pressed = false;
    status_led_notify_key_event(key->index, false);
}

static void keyboard_custom_handle_timers(keyboard_custom_key_t *key, TickType_t now)
{
    if (!key->initialized) {
        return;
    }

    if (key->pressed && !key->long_sent &&
        keyboard_custom_elapsed_ms(now, key->press_tick) >= KEYBOARD_CUSTOM_LONG_PRESS_MS) {
        keyboard_custom_cancel_pending_single(key);
        key->long_sent = true;
        keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_LONG);
        return;
    }

    if (!key->pressed && key->pending_single &&
        keyboard_custom_tick_reached(now, key->pending_single_due_tick)) {
        keyboard_custom_cancel_pending_single(key);
        keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_SINGLE);
    }
}

static void keyboard_custom_apply_stable_transition(
    keyboard_custom_key_t *key,
    bool raw_high,
    TickType_t now,
    const char *origin)
{
    if (!key->initialized || raw_high == key->stable_level_high) {
        return;
    }

    key->last_sample_high = raw_high;
    key->stable_level_high = raw_high;
    key->stable_count = KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES;
    bool pressed = !raw_high;
    power_manager_record_activity(key->logical_name);
    status_led_notify_key_event(key->index, pressed);
    key->raw_feedback_pressed = pressed;
    ESP_LOGI(
        TAG,
        "custom key stable transition: logical=%s source=%s raw_high=%d pressed=%d origin=%s",
        key->logical_name,
        key->label,
        raw_high ? 1 : 0,
        pressed ? 1 : 0,
        origin != NULL ? origin : "unknown");
    keyboard_input_debug_log(
        KEYBOARD_INPUT_DEBUG_KEY_STABLE,
        key->logical_key,
        raw_high ? 1u : 0u,
        pressed ? 1u : 0u);

    if (pressed && !key->pressed) {
        key->pressed = true;
        key->press_tick = now;
        key->long_sent = false;
        key->double_candidate = key->pending_single;
        keyboard_custom_log_event(key, KEYBOARD_CUSTOM_PHASE_PRESS, key->single_usage, ESP_OK);
    } else if (!pressed && key->pressed) {
        key->pressed = false;
        keyboard_custom_log_event(key, KEYBOARD_CUSTOM_PHASE_RELEASE, key->single_usage, ESP_OK);
        ESP_LOGI(
            TAG,
            "custom key release: logical=%s source=%s usage=F%u",
            key->logical_name,
            key->label,
            keyboard_custom_function_number(key->single_usage));

        if (key->long_sent) {
            key->long_sent = false;
            keyboard_custom_cancel_pending_single(key);
            status_led_notify_key_feedback(key->index, STATUS_LED_KEY_FEEDBACK_LONG);
        } else if (keyboard_custom_elapsed_ms(now, key->press_tick) >= KEYBOARD_CUSTOM_LONG_PRESS_MS) {
            keyboard_custom_cancel_pending_single(key);
            keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_LONG);
        } else if (key->double_candidate && key->pending_single) {
            keyboard_custom_cancel_pending_single(key);
            keyboard_custom_send_gesture(key, KEYBOARD_CUSTOM_GESTURE_DOUBLE);
        } else {
            key->pending_single = true;
            key->double_candidate = false;
            key->pending_single_due_tick = now + pdMS_TO_TICKS(KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS);
            ESP_LOGI(
                TAG,
                "custom key single pending: logical=%s source=%s usage=F%u window_ms=%d",
                key->logical_name,
                key->label,
                keyboard_custom_function_number(key->single_usage),
                KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS);
        }
    }
}

static void keyboard_custom_handle_sample(keyboard_custom_key_t *key, bool raw_high, TickType_t now)
{
    if (!key->initialized) {
        key->initialized = true;
        key->last_sample_high = raw_high;
        key->stable_level_high = raw_high;
        key->stable_count = 1;
        key->pressed = false;
        key->long_sent = false;
        key->pending_single = false;
        key->double_candidate = false;
        key->raw_feedback_pressed = false;
        ESP_LOGI(
            TAG,
            "custom key idle detected: logical=%s source=%s raw_high=%d",
            key->logical_name,
            key->label,
            raw_high ? 1 : 0);
        return;
    }

    if (raw_high == key->last_sample_high) {
        if (key->stable_count < UINT8_MAX) {
            key->stable_count++;
        }
    } else {
        if (!raw_high) {
            keyboard_custom_apply_raw_feedback(key, now, "raw_edge");
        } else {
            keyboard_custom_clear_raw_feedback(key);
        }
        ESP_LOGI(
            TAG,
            "custom key raw transition: logical=%s source=%s raw_high=%d stable_high=%d",
            key->logical_name,
            key->label,
            raw_high ? 1 : 0,
            key->stable_level_high ? 1 : 0);
        keyboard_input_debug_log(
            KEYBOARD_INPUT_DEBUG_KEY_RAW,
            key->logical_key,
            raw_high ? 1u : 0u,
            key->stable_level_high ? 1u : 0u);
        key->last_sample_high = raw_high;
        key->stable_count = 1;
        return;
    }

    if (key->stable_count < KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES || raw_high == key->stable_level_high) {
        return;
    }

    keyboard_custom_apply_stable_transition(key, raw_high, now, "poll");
}

static bool keyboard_custom_generated_raw_high(
    const keyboard_custom_key_t *key,
    bool physical_raw_high,
    TickType_t now)
{
    keyboard_custom_generated_state_t *state = &s_custom_generated_states[key->index];
    if (!state->active) {
        return physical_raw_high;
    }

    uint32_t elapsed_ms = keyboard_custom_elapsed_ms(now, state->started_tick);
    uint32_t press_ms = KEYBOARD_CUSTOM_GENERATED_PRESS_MS;
    uint32_t release_ms = KEYBOARD_CUSTOM_GENERATED_RELEASE_SETTLE_MS;
    if (state->gesture == KEYBOARD_CUSTOM_GESTURE_LONG) {
        press_ms = KEYBOARD_CUSTOM_LONG_PRESS_MS + KEYBOARD_CUSTOM_GENERATED_PRESS_MS;
    }

    if (elapsed_ms < press_ms) {
        return false;
    }
    if (elapsed_ms < press_ms + release_ms) {
        return true;
    }
    if (state->gesture == KEYBOARD_CUSTOM_GESTURE_DOUBLE) {
        if (elapsed_ms < press_ms + release_ms + KEYBOARD_CUSTOM_GENERATED_PRESS_MS) {
            return false;
        }
        if (elapsed_ms < press_ms + release_ms + KEYBOARD_CUSTOM_GENERATED_PRESS_MS + release_ms) {
            return true;
        }
    }

    state->active = false;
    ESP_LOGI(
        TAG,
        "custom key generated gesture completed: logical=%s source=%s gesture=%s",
        key->logical_name,
        key->label,
        keyboard_custom_gesture_name(state->gesture));
    return physical_raw_high;
}

static void keyboard_custom_drain_generated_events(TickType_t now)
{
    if (s_custom_generated_event_queue == NULL) {
        return;
    }

    keyboard_custom_generated_event_t event = {0};
    while (xQueueReceive(s_custom_generated_event_queue, &event, 0) == pdTRUE) {
        keyboard_custom_key_t *key = keyboard_custom_find_key(event.logical_key);
        if (key == NULL) {
            ESP_LOGW(TAG, "drop generated custom key event: logical=%u", event.logical_key);
            continue;
        }
        if (!key->initialized) {
            keyboard_custom_handle_sample(key, true, now);
        }
        keyboard_custom_generated_state_t *state = &s_custom_generated_states[key->index];
        state->active = true;
        state->gesture = event.gesture;
        state->started_tick = now;
        ESP_LOGI(
            TAG,
            "custom key generated gesture armed: logical=%s source=%s gesture=%s",
            key->logical_name,
            key->label,
            keyboard_custom_gesture_name(event.gesture));
    }
}

static bool keyboard_custom_generated_active(void)
{
    for (size_t index = 0; index < sizeof(s_custom_generated_states) / sizeof(s_custom_generated_states[0]); ++index) {
        if (s_custom_generated_states[index].active) {
            return true;
        }
    }
    return false;
}

static bool keyboard_custom_debounce_active(const keyboard_custom_key_t *key)
{
    return key->initialized &&
           key->last_sample_high != key->stable_level_high &&
           key->stable_count < KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES;
}

static uint32_t keyboard_custom_next_wait_ms(TickType_t now)
{
    if (keyboard_custom_generated_active()) {
        return KEYBOARD_CUSTOM_POLL_MS;
    }

    uint32_t wait_ms = keyboard_power_state_is_low_power_idle()
        ? KEYBOARD_CUSTOM_LOW_POWER_IDLE_BACKUP_POLL_MS
        : KEYBOARD_CUSTOM_IDLE_BACKUP_POLL_MS;
    for (size_t index = 0; index < sizeof(s_custom_keys) / sizeof(s_custom_keys[0]); ++index) {
        keyboard_custom_key_t *key = &s_custom_keys[index];
        if (!key->initialized || keyboard_custom_debounce_active(key)) {
            return KEYBOARD_CUSTOM_POLL_MS;
        }
        if (key->pressed && !key->long_sent) {
            uint32_t remaining_ms = keyboard_custom_ticks_until_ms(
                now,
                key->press_tick + pdMS_TO_TICKS(KEYBOARD_CUSTOM_LONG_PRESS_MS));
            if (remaining_ms == 0) {
                return KEYBOARD_CUSTOM_POLL_MS;
            }
            if (remaining_ms < wait_ms) {
                wait_ms = remaining_ms;
            }
        }
        if (!key->pressed && key->pending_single) {
            uint32_t remaining_ms = keyboard_custom_ticks_until_ms(now, key->pending_single_due_tick);
            if (remaining_ms == 0) {
                return KEYBOARD_CUSTOM_POLL_MS;
            }
            if (remaining_ms < wait_ms) {
                wait_ms = remaining_ms;
            }
        }
    }
    return wait_ms;
}

static void keyboard_custom_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("keyboard_custom_task");

    while (1) {
        watchdog_platform_feed_current_task();
        TickType_t now = xTaskGetTickCount();
        keyboard_custom_drain_generated_events(now);
        for (size_t index = 0; index < sizeof(s_custom_keys) / sizeof(s_custom_keys[0]); ++index) {
            keyboard_custom_handle_timers(&s_custom_keys[index], now);
            int level = gpio_get_level(s_custom_keys[index].gpio);
            bool raw_high = keyboard_custom_generated_raw_high(&s_custom_keys[index], level != 0, now);
            keyboard_custom_handle_sample(&s_custom_keys[index], raw_high, now);
            keyboard_custom_handle_timers(&s_custom_keys[index], now);
        }
        uint32_t wait_ms = keyboard_custom_next_wait_ms(xTaskGetTickCount());
        if (keyboard_power_state_is_low_power_idle() && wait_ms > KEYBOARD_CUSTOM_POLL_MS) {
            (void)watchdog_platform_task_notify_take_low_power(pdTRUE, wait_ms);
        } else {
            (void)watchdog_platform_task_notify_take(pdTRUE, wait_ms);
        }
    }
}

static void keyboard_ec11_queue_edge_from_isr(void *arg)
{
    (void)arg;
    if (s_ec11_event_queue == NULL) {
        return;
    }

    uint8_t raw_state = keyboard_ec11_read_raw_state();
    if (s_ec11_isr_last_raw_valid && raw_state == s_ec11_isr_last_raw_state) {
        return;
    }
    s_ec11_isr_last_raw_state = raw_state;
    s_ec11_isr_last_raw_valid = true;

    keyboard_ec11_event_t event = {
        .raw_state = raw_state,
    };
    BaseType_t higher_priority_woken = pdFALSE;
    if (xQueueSendFromISR(s_ec11_event_queue, &event, &higher_priority_woken) != pdTRUE) {
        s_ec11_state.isr_drop_count++;
        s_ec11_overflow_raw_state = raw_state;
        s_ec11_overflow_pending = true;
    }
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void keyboard_ec11_handle_state(keyboard_ec11_state_t *state, uint8_t raw_state)
{
    if (!state->initialized) {
        state->initialized = true;
        state->last_state = raw_state;
        ESP_LOGI(
            TAG,
            "EC11 idle detected: a_gpio=%d b_gpio=%d state=0x%02x active_low=1",
            (int)state->a_gpio,
            (int)state->b_gpio,
            raw_state);
        return;
    }

    if (raw_state == state->last_state) {
        return;
    }

    int8_t delta = keyboard_ec11_quadrature_delta(state->last_state, raw_state);
    uint8_t previous_state = state->last_state;
    state->last_state = raw_state;
    if (delta == 0) {
        state->invalid_transition_count++;
        state->detent_accumulator = 0;
        ESP_LOGD(
            TAG,
            "EC11 ignored invalid transition: previous=0x%02x state=0x%02x invalid_count=%" PRIu32,
            previous_state,
            raw_state,
            state->invalid_transition_count);
        keyboard_input_debug_log(
            KEYBOARD_INPUT_DEBUG_EC11_INVALID,
            ((uint32_t)previous_state << 8) | raw_state,
            0,
            state->invalid_transition_count);
        return;
    }

    state->detent_accumulator += delta;
    const bool was_low_power_idle = keyboard_power_state_is_low_power_idle();
    power_manager_record_activity("ec11_rotate");
    int8_t feedback_delta =
        keyboard_ec11_feedback_delta_from_accumulator(state->detent_accumulator);
    if ((was_low_power_idle || raw_state != KEYBOARD_EC11_DETENT_STATE) &&
        feedback_delta != 0) {
        keyboard_ec11_refresh_feedback_for_delta(state, feedback_delta, was_low_power_idle);
    }
    ESP_LOGD(
        TAG,
        "EC11 transition: previous=0x%02x state=0x%02x delta=%d accumulator=%" PRId32,
        previous_state,
        raw_state,
        (int)delta,
        state->detent_accumulator);
    keyboard_input_debug_log(
        KEYBOARD_INPUT_DEBUG_EC11_TRANSITION,
        ((uint32_t)previous_state << 8) | raw_state,
        keyboard_ec11_delta_code(delta),
        (uint32_t)state->detent_accumulator);

    if (raw_state != KEYBOARD_EC11_DETENT_STATE) {
        return;
    }

    if (state->detent_accumulator >= 4) {
        state->detent_accumulator = 0;
        state->clockwise_count++;
        ESP_LOGI(TAG, "EC11 detent: direction=clockwise count=%" PRIu32, state->clockwise_count);
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_EC11_DETENT, DIAG_SEV_INFO,
                 1, state->clockwise_count, 0, 0);
        (void)keyboard_ec11_dispatch_rotation(EC11_ROTATION_DIRECTION_CW, "ec11.detent.cw");
    } else if (state->detent_accumulator <= -4) {
        state->detent_accumulator = 0;
        state->counter_clockwise_count++;
        ESP_LOGI(TAG, "EC11 detent: direction=counter_clockwise count=%" PRIu32,
                 state->counter_clockwise_count);
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_EC11_DETENT, DIAG_SEV_INFO,
                 2, state->counter_clockwise_count, 0, 0);
        (void)keyboard_ec11_dispatch_rotation(EC11_ROTATION_DIRECTION_CCW, "ec11.detent.ccw");
    } else {
        ESP_LOGD(
            TAG,
            "EC11 returned to detent without full step: accumulator=%" PRId32,
            state->detent_accumulator);
        keyboard_input_debug_log(
            KEYBOARD_INPUT_DEBUG_EC11_PARTIAL,
            raw_state,
            0,
            (uint32_t)state->detent_accumulator);
        state->detent_accumulator = 0;
    }
}

static const char *keyboard_ec11_source_for_action(
    ec11_rotation_direction_t direction,
    ec11_rotation_action_t action,
    char *buffer,
    size_t buffer_size)
{
    snprintf(
        buffer,
        buffer_size,
        "ec11.rotate.%s.%s",
        ec11_rotation_control_direction_name(direction),
        ec11_rotation_control_action_name(action));
    return buffer;
}

static esp_err_t keyboard_ec11_dispatch_rotation(
    ec11_rotation_direction_t direction,
    const char *source)
{
    ec11_rotation_action_t action = ec11_rotation_control_get_action();
    uint16_t usage = 0;
    switch (action) {
    case EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS:
        usage = direction == EC11_ROTATION_DIRECTION_CW
            ? HID_CONSUMER_USAGE_BRIGHTNESS_INCREMENT
            : HID_CONSUMER_USAGE_BRIGHTNESS_DECREMENT;
        break;
    case EC11_ROTATION_ACTION_DISABLED:
        keyboard_input_debug_log(
            KEYBOARD_INPUT_DEBUG_EC11_DISPATCH,
            (uint32_t)direction,
            keyboard_ec11_action_code(action),
            0);
        ESP_LOGI(
            TAG,
            "EC11 rotation disabled: direction=%s source=%s",
            ec11_rotation_control_direction_name(direction),
            source != NULL ? source : "unknown");
        return ESP_OK;
    case EC11_ROTATION_ACTION_SYSTEM_VOLUME:
    default:
        usage = direction == EC11_ROTATION_DIRECTION_CW
            ? HID_CONSUMER_USAGE_VOLUME_INCREMENT
            : HID_CONSUMER_USAGE_VOLUME_DECREMENT;
        break;
    }

    char source_label[48];
    status_led_notify_ec11_feedback(direction == EC11_ROTATION_DIRECTION_CW
        ? STATUS_LED_EC11_FEEDBACK_ROTATE_CW
        : STATUS_LED_EC11_FEEDBACK_ROTATE_CCW);
    esp_err_t ret = ble_hid_send_consumer_usage_async(
        usage,
        keyboard_ec11_source_for_action(direction, action, source_label, sizeof(source_label)));
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(
            TAG,
            "EC11 rotation local feedback only: action=%s direction=%s usage=0x%04X source=%s error=%s",
            ec11_rotation_control_action_name(action),
            ec11_rotation_control_direction_name(direction),
            usage,
            source != NULL ? source : "unknown",
            esp_err_to_name(ret));
        return ret;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "EC11 rotation HID dispatch skipped: action=%s direction=%s usage=0x%04X source=%s error=%s local_feedback=kept",
            ec11_rotation_control_action_name(action),
            ec11_rotation_control_direction_name(direction),
            usage,
            source != NULL ? source : "unknown",
            esp_err_to_name(ret));
        return ret;
    }

    keyboard_input_debug_log(
        KEYBOARD_INPUT_DEBUG_EC11_DISPATCH,
        (uint32_t)direction,
        keyboard_ec11_action_code(action),
        usage);
    ESP_LOGI(
        TAG,
        "EC11 rotation queued: action=%s direction=%s usage=0x%04X source=%s",
        ec11_rotation_control_action_name(action),
        ec11_rotation_control_direction_name(direction),
        usage,
        source != NULL ? source : "unknown");
    return ESP_OK;
}

static esp_err_t keyboard_ble_control_write(
    const uint8_t *data,
    size_t len,
    const char *source)
{
    if (data == NULL || len == 0 || len >= 64) {
        return ESP_ERR_INVALID_SIZE;
    }

    char command[64];
    memcpy(command, data, len);
    command[len] = '\0';
    command[strcspn(command, "\r\n")] = '\0';

    esp_err_t ec11_ret = ESP_OK;
    if (ec11_rotation_control_consume_command(command, source, &ec11_ret)) {
        return ec11_ret;
    }

    esp_err_t device_settings_ret = device_settings_consume_control_command(command);
    if (device_settings_ret != ESP_ERR_NOT_FOUND) {
        power_manager_record_activity("device_settings_ble_control");
        if (device_settings_ret == ESP_OK) {
            status_led_apply_device_settings();
        }
        return device_settings_ret;
    }

    return voice_recording_control_dispatch_control_command(command, source);
}

static uint32_t keyboard_ec11_backup_poll_ms(void)
{
    if (keyboard_power_state_is_low_power_idle()) {
        return KEYBOARD_EC11_LOW_POWER_IDLE_POLL_MS;
    }
    return KEYBOARD_EC11_IDLE_POLL_MS;
}

static void keyboard_ec11_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("keyboard_ec11_task");
    keyboard_ec11_handle_state(&s_ec11_state, keyboard_ec11_read_raw_state());

    while (1) {
        watchdog_platform_feed_current_task();
        keyboard_ec11_event_t event = {0};
        if (xQueueReceive(
                s_ec11_event_queue,
                &event,
                pdMS_TO_TICKS(keyboard_ec11_backup_poll_ms())) == pdTRUE) {
            keyboard_ec11_handle_state(&s_ec11_state, event.raw_state);
            while (xQueueReceive(s_ec11_event_queue, &event, 0) == pdTRUE) {
                keyboard_ec11_handle_state(&s_ec11_state, event.raw_state);
            }
            if (s_ec11_overflow_pending) {
                uint8_t overflow_raw_state = s_ec11_overflow_raw_state;
                s_ec11_overflow_pending = false;
                keyboard_ec11_handle_state(&s_ec11_state, overflow_raw_state);
            }
            keyboard_ec11_handle_state(&s_ec11_state, keyboard_ec11_read_raw_state());
            continue;
        }

        /* Poll slowly as a backup in case an edge is missed while interrupts are being reconfigured. */
        keyboard_ec11_handle_state(&s_ec11_state, keyboard_ec11_read_raw_state());
        if (s_ec11_state.isr_drop_count != 0) {
            TickType_t now = xTaskGetTickCount();
            TickType_t log_interval_ticks = pdMS_TO_TICKS(KEYBOARD_EC11_DROP_LOG_INTERVAL_MS);
            if (log_interval_ticks == 0) {
                log_interval_ticks = 1;
            }
            if (s_ec11_state.last_drop_log_tick == 0 ||
                (now - s_ec11_state.last_drop_log_tick) >= log_interval_ticks) {
                ESP_LOGW(
                    TAG,
                    "EC11 ISR event queue dropped edges: drop_count=%" PRIu32,
                    s_ec11_state.isr_drop_count);
                s_ec11_state.isr_drop_count = 0;
                s_ec11_state.last_drop_log_tick = now;
            }
        }
    }
}

static esp_err_t keyboard_custom_start(void)
{
    if (s_custom_task_handle != NULL) {
        return ESP_OK;
    }

    if (s_custom_generated_event_queue == NULL) {
        s_custom_generated_event_queue = xQueueCreate(
            KEYBOARD_CUSTOM_GENERATED_EVENT_QUEUE_DEPTH,
            sizeof(keyboard_custom_generated_event_t));
        if (s_custom_generated_event_queue == NULL) {
            ESP_LOGE(TAG, "custom key generated event queue create failed");
            return ESP_ERR_NO_MEM;
        }
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_PINS_KEY1_IO) |
                        (1ULL << BOARD_PINS_KEY2_IO) |
                        (1ULL << BOARD_PINS_KEY3_IO) |
                        (1ULL << BOARD_PINS_KEY4_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "custom key GPIO config failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_GPIO_FAIL, DIAG_SEV_ERROR, 1, ret, 0, 0);
        return ret;
    }
    for (size_t index = 0; index < sizeof(s_custom_keys) / sizeof(s_custom_keys[0]); ++index) {
        keyboard_enable_active_low_light_sleep_wake(
            s_custom_keys[index].gpio,
            s_custom_keys[index].logical_name);
    }

    BaseType_t task_ok = xTaskCreate(
        keyboard_custom_task,
        "keyboard_custom_task",
        3072,
        NULL,
        4,
        &s_custom_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "custom key task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "custom keys ready: key1=gpio38:f13/f17/f21 key2=gpio39:f14/f18/f22 key3=gpio40:f15/f19/f23 key4=gpio41:f16/f20/f24 active_low=1 wake=active_low_gpio_wakeup+20ms_scan low_power_wake=active_low_gpio_wakeup+20ms_scan poll_ms=%d idle_backup_ms=%d low_power_idle_backup_ms=%d debounce_ms=%d debounce_samples=%d double_ms=%d long_ms=%d",
        KEYBOARD_CUSTOM_POLL_MS,
        KEYBOARD_CUSTOM_IDLE_BACKUP_POLL_MS,
        KEYBOARD_CUSTOM_LOW_POWER_IDLE_BACKUP_POLL_MS,
        KEYBOARD_CUSTOM_DEBOUNCE_MS,
        KEYBOARD_CUSTOM_DEBOUNCE_SAMPLES,
        KEYBOARD_CUSTOM_DOUBLE_CLICK_WINDOW_MS,
        KEYBOARD_CUSTOM_LONG_PRESS_MS);
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
    if (s_ec11_event_queue == NULL) {
        s_ec11_event_queue = xQueueCreate(KEYBOARD_EC11_EVENT_QUEUE_DEPTH, sizeof(keyboard_ec11_event_t));
        if (s_ec11_event_queue == NULL) {
            ESP_LOGE(TAG, "EC11 event queue create failed");
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_PINS_EC11_A_IO) |
                        (1ULL << BOARD_PINS_EC11_B_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 GPIO config failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_GPIO_FAIL, DIAG_SEV_ERROR, 2, ret, 0, 0);
        return ret;
    }

    s_ec11_isr_last_raw_state = keyboard_ec11_read_raw_state();
    s_ec11_isr_last_raw_valid = true;
    s_ec11_overflow_pending = false;
    s_ec11_overflow_raw_state = s_ec11_isr_last_raw_state;

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "EC11 GPIO ISR service install failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_GPIO_FAIL, DIAG_SEV_ERROR, 2, ret, 0, 0);
        return ret;
    }
    keyboard_enable_active_low_light_sleep_wake(BOARD_PINS_EC11_A_IO, "EC11_A");
    keyboard_enable_active_low_light_sleep_wake(BOARD_PINS_EC11_B_IO, "EC11_B");
    ret = gpio_isr_handler_add(BOARD_PINS_EC11_A_IO, keyboard_ec11_queue_edge_from_isr, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EC11 A ISR handler add failed: %s", esp_err_to_name(ret));
        diag_log(DIAG_SRC_KEYBOARD, DIAG_KBD_GPIO_FAIL, DIAG_SEV_ERROR, 2, ret, 0, 0);
        return ret;
    }
    ret = gpio_isr_handler_add(BOARD_PINS_EC11_B_IO, keyboard_ec11_queue_edge_from_isr, NULL);
    if (ret != ESP_OK) {
        (void)gpio_isr_handler_remove(BOARD_PINS_EC11_A_IO);
        ESP_LOGE(TAG, "EC11 B ISR handler add failed: %s", esp_err_to_name(ret));
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
        "EC11 ready: a=gpio42 b=gpio2 key=gpio18 key_policy=gpio18_power_on_runtime_custom_single_click_double_click_recovery_pwr_hold_gpio9_shutdown_separate decoder=interrupt_quadrature direction_policy=clockwise_increases_volume_brightness detent_state=0x%02x idle_poll_ms=%d low_power_idle_poll_ms=%d queue_depth=%d",
        KEYBOARD_EC11_DETENT_STATE,
        KEYBOARD_EC11_IDLE_POLL_MS,
        KEYBOARD_EC11_LOW_POWER_IDLE_POLL_MS,
        KEYBOARD_EC11_EVENT_QUEUE_DEPTH);
    return ESP_OK;
}

esp_err_t keyboard_start(void)
{
    hid_keyboard_init();
    ble_hid_register_usb_command_handler(keyboard_consume_usb_command);
    ec11_rotation_control_register_dispatcher(keyboard_ec11_dispatch_rotation);

    esp_err_t custom_ret = keyboard_custom_start();
    if (custom_ret != ESP_OK) {
        return custom_ret;
    }

    esp_err_t ec11_ret = keyboard_ec11_start();
    if (ec11_ret != ESP_OK && ec11_ret != ESP_ERR_NOT_SUPPORTED) {
        return ec11_ret;
    }

    esp_err_t voice_ret = voice_recording_control_start();
    ble_audio_stream_set_control_write_handler(keyboard_ble_control_write);
    if (voice_ret != ESP_OK) {
        ESP_LOGW(TAG, "voice recording control started degraded: %s", esp_err_to_name(voice_ret));
    }

    if (voice_ret != ESP_OK) {
        return voice_ret;
    }
    return ESP_OK;
}

esp_err_t keyboard_start_safe_mode(void)
{
    hid_keyboard_init();
    ble_hid_register_usb_command_handler(keyboard_consume_usb_command);
    ec11_rotation_control_register_dispatcher(keyboard_ec11_dispatch_rotation);
    ble_audio_stream_set_control_write_handler(keyboard_ble_control_write);
    ESP_LOGW(TAG, "safe mode: voice recording control and audio capture are disabled");
    esp_err_t custom_ret = keyboard_custom_start();
    if (custom_ret != ESP_OK) {
        return custom_ret;
    }
    esp_err_t ec11_ret = keyboard_ec11_start();
    return ec11_ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ec11_ret;
}

uint32_t keyboard_get_key_press_count(void)
{
    return hid_keyboard_get_key_press_count();
}
