#include "voice_recording_control.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_capture.h"
#include "voice_key_input.h"

#define VOICE_RECORDING_CONTROL_PREFIX_CHAR '~'
#define VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES 32
#define VOICE_RECORDING_CONTROL_VREC_PREFIX "VREC:"
#define VOICE_RECORDING_CONTROL_SESSION_CHECK_MS 50

typedef enum {
    VOICE_RECORDING_STATE_IDLE = 0,
    VOICE_RECORDING_STATE_RECORDING,
} voice_recording_state_t;

static const char *TAG = "voice_rec_ctrl";

static bool s_started;
static TaskHandle_t s_task_handle;
static bool s_usb_command_active;
static size_t s_usb_command_length;
static char s_usb_command_buffer[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
static voice_recording_state_t s_state = VOICE_RECORDING_STATE_IDLE;
static bool s_cancel_pending;
static const char *s_cancel_source;

static esp_err_t voice_recording_control_enter_recording(const char *source)
{
    esp_err_t ret = audio_capture_session_begin();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "recording start rejected source=%s: %s", source, esp_err_to_name(ret));
        return ret;
    }

    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_state = VOICE_RECORDING_STATE_RECORDING;
    ESP_LOGI(TAG, "recording start source=%s", source);
    return ESP_OK;
}

static esp_err_t voice_recording_control_exit_recording(const char *source)
{
    esp_err_t ret = audio_capture_session_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "recording stop rejected source=%s: %s", source, esp_err_to_name(ret));
        return ret;
    }

    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_state = VOICE_RECORDING_STATE_IDLE;
    ESP_LOGI(TAG, "recording stop source=%s", source);
    return ESP_OK;
}

static void voice_recording_control_toggle(const char *source)
{
    if (s_cancel_pending) {
        ESP_LOGW(TAG, "recording toggle ignored source=%s: cancel pending", source);
        return;
    }

    if (s_state == VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_enter_recording(source);
    } else {
        voice_recording_control_exit_recording(source);
    }
}

static void voice_recording_control_cancel(const char *source)
{
    esp_err_t ret = audio_capture_session_cancel();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "recording cancel rejected source=%s: %s", source, esp_err_to_name(ret));
        return;
    }

    s_cancel_pending = true;
    s_cancel_source = source;
    if (!audio_capture_session_is_active()) {
        s_cancel_pending = false;
        s_cancel_source = NULL;
        s_state = VOICE_RECORDING_STATE_IDLE;
        ESP_LOGI(TAG, "recording cancel source=%s", source);
        return;
    }

    ESP_LOGI(TAG, "recording cancel requested source=%s", source);
}

static void voice_recording_control_task(void *parameter)
{
    (void)parameter;

    while (1) {
        if (voice_key_input_take_toggle_event()) {
            voice_recording_control_toggle("key1");
        }

        if (s_state == VOICE_RECORDING_STATE_RECORDING && !audio_capture_session_is_active()) {
            if (s_cancel_pending) {
                const char *source = s_cancel_source != NULL ? s_cancel_source : "unknown";
                s_cancel_pending = false;
                s_cancel_source = NULL;
                s_state = VOICE_RECORDING_STATE_IDLE;
                ESP_LOGI(TAG, "recording cancel source=%s", source);
                continue;
            }

            s_state = VOICE_RECORDING_STATE_IDLE;
            ESP_LOGI(TAG, "recording session finished");
        }
    }
}

esp_err_t voice_recording_control_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(audio_capture_start());
    ESP_ERROR_CHECK(voice_key_input_start());

    BaseType_t task_ok = xTaskCreate(
        voice_recording_control_task,
        "voice_recording_control_task",
        4096,
        NULL,
        5,
        &s_task_handle);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "voice recording control ready: key1 toggle start/stop");
    return ESP_OK;
}

bool voice_recording_control_consume_usb_control_byte(uint8_t input_char)
{
    if (!s_usb_command_active) {
        if (input_char != (uint8_t)VOICE_RECORDING_CONTROL_PREFIX_CHAR) {
            return false;
        }
        s_usb_command_active = true;
        s_usb_command_length = 0;
        memset(s_usb_command_buffer, 0, sizeof(s_usb_command_buffer));
        return true;
    }

    if (input_char == '\r') {
        return true;
    }

    if (input_char == '\n') {
        s_usb_command_active = false;
        s_usb_command_buffer[s_usb_command_length] = '\0';

        if (strncmp(
                s_usb_command_buffer,
                VOICE_RECORDING_CONTROL_VREC_PREFIX,
                strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX)) == 0) {
            const char *action = s_usb_command_buffer + strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX);
            if (strcmp(action, "TOGGLE") == 0) {
                voice_recording_control_toggle("usb");
            } else if (strcmp(action, "CANCEL") == 0) {
                voice_recording_control_cancel("usb");
            } else {
                ESP_LOGW(TAG, "drop control command: %s", s_usb_command_buffer);
            }
            return true;
        }

        ESP_LOGW(TAG, "drop control command: %s", s_usb_command_buffer);
        return true;
    }

    if (s_usb_command_length + 1 >= sizeof(s_usb_command_buffer)) {
        s_usb_command_active = false;
        s_usb_command_length = 0;
        memset(s_usb_command_buffer, 0, sizeof(s_usb_command_buffer));
        ESP_LOGW(TAG, "drop control command: too long");
        return true;
    }

    s_usb_command_buffer[s_usb_command_length++] = (char)input_char;
    return true;
}
