#include "voice_recording_control.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "diag_log.h"

#include "audio_capture.h"
#include "ble_audio_stream.h"
#include "ble_hid_gap.h"
#include "power_manager.h"
#include "voice_key_input.h"
#include "watchdog_platform.h"

#define VOICE_RECORDING_CONTROL_PREFIX_CHAR '~'
#define VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES 32
#define VOICE_RECORDING_CONTROL_VREC_PREFIX "VREC:"
#define VOICE_RECORDING_CONTROL_SESSION_CHECK_MS 50
#define VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS 12000
#define VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS 250

typedef enum {
    VOICE_RECORDING_STATE_IDLE = 0,
    VOICE_RECORDING_STATE_RECORDING,
    VOICE_RECORDING_STATE_TRANSFERRING,
    VOICE_RECORDING_STATE_RECOVERY,
} voice_recording_state_t;

typedef enum {
    VOICE_RECORDING_FLOW_TOGGLE_START = 1,
    VOICE_RECORDING_FLOW_TOGGLE_STOP = 2,
    VOICE_RECORDING_FLOW_START_OK = 3,
    VOICE_RECORDING_FLOW_STOP_REQUESTED = 4,
    VOICE_RECORDING_FLOW_PENDING_START = 5,
    VOICE_RECORDING_FLOW_PENDING_READY = 6,
    VOICE_RECORDING_FLOW_PENDING_TIMEOUT = 7,
    VOICE_RECORDING_FLOW_SESSION_FINISHED = 8,
    VOICE_RECORDING_FLOW_SESSION_ABORTED = 9,
    VOICE_RECORDING_FLOW_TOGGLE_IGNORED = 10,
    VOICE_RECORDING_FLOW_CANCEL = 11,
    VOICE_RECORDING_FLOW_RECOVERY = 12,
    VOICE_RECORDING_FLOW_START_REJECTED = 13,
    VOICE_RECORDING_FLOW_STOP_REJECTED = 14,
} voice_recording_flow_stage_t;

static const char *TAG = "voice_rec_ctrl";

static bool s_started;
static TaskHandle_t s_task_handle;
static bool s_usb_command_active;
static size_t s_usb_command_length;
static char s_usb_command_buffer[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
static voice_recording_state_t s_state = VOICE_RECORDING_STATE_IDLE;
static bool s_cancel_pending;
static const char *s_cancel_source;
static bool s_pending_start;
static const char *s_pending_start_source;
static TickType_t s_pending_start_deadline_tick;
static TickType_t s_pending_start_next_retry_tick;
static const char *s_active_session_source;
static uint32_t s_session_count;

static uint32_t voice_recording_source_code(const char *source)
{
    if (source == NULL) {
        return 0;
    }
    if (strncmp(source, "voice", strlen("voice")) == 0 ||
        strncmp(source, "key1", strlen("key1")) == 0 ||
        strncmp(source, "ec11", strlen("ec11")) == 0) {
        return 1;
    }
    if (strcmp(source, "usb") == 0) {
        return 2;
    }
    return 255;
}

static const char *voice_recording_state_name(voice_recording_state_t state)
{
    switch (state) {
    case VOICE_RECORDING_STATE_IDLE:
        return "idle";
    case VOICE_RECORDING_STATE_RECORDING:
        return "recording";
    case VOICE_RECORDING_STATE_TRANSFERRING:
        return "transferring";
    case VOICE_RECORDING_STATE_RECOVERY:
        return "recovery";
    default:
        return "unknown";
    }
}

static void voice_recording_control_log_flow(
    voice_recording_flow_stage_t stage,
    const char *event,
    const char *source,
    esp_err_t result,
    bool warn)
{
    const char *safe_source = source != NULL ? source : "unknown";
    bool audio_active = audio_capture_session_is_active();
    bool ble_ready = ble_audio_stream_is_ready();
    const char *result_name = result == ESP_OK ? "ESP_OK" : esp_err_to_name(result);

    if (warn) {
        ESP_LOGW(
            TAG,
            "voiceflow event=%s source=%s result=%s state=%s pending=%u cancel_pending=%u session_count=%" PRIu32 " audio_active=%u ble_ready=%u",
            event,
            safe_source,
            result_name,
            voice_recording_state_name(s_state),
            s_pending_start ? 1u : 0u,
            s_cancel_pending ? 1u : 0u,
            s_session_count,
            audio_active ? 1u : 0u,
            ble_ready ? 1u : 0u);
    } else {
        ESP_LOGI(
            TAG,
            "voiceflow event=%s source=%s result=%s state=%s pending=%u cancel_pending=%u session_count=%" PRIu32 " audio_active=%u ble_ready=%u",
            event,
            safe_source,
            result_name,
            voice_recording_state_name(s_state),
            s_pending_start ? 1u : 0u,
            s_cancel_pending ? 1u : 0u,
            s_session_count,
            audio_active ? 1u : 0u,
            ble_ready ? 1u : 0u);
    }

    diag_log(
        DIAG_SRC_VOICE_REC,
        DIAG_VREC_FLOW,
        warn ? DIAG_SEV_WARN : DIAG_SEV_INFO,
        (uint32_t)stage,
        voice_recording_source_code(safe_source),
        s_session_count,
        (uint32_t)s_state);
}

static void voice_recording_control_log_device_status(const char *state, const char *detail)
{
    ESP_LOGI(TAG, "device_status state=%s detail=%s", state, detail != NULL ? detail : "none");
}

static void voice_recording_control_log_device_error(const char *state, const char *detail, esp_err_t ret)
{
    ESP_LOGW(TAG, "device_status state=%s detail=%s error=%s", state, detail != NULL ? detail : "none", esp_err_to_name(ret));
}

static void voice_recording_control_clear_power_blockers(void)
{
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        false);
}

static bool voice_recording_control_tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void voice_recording_control_reset_pending_start(void)
{
    s_pending_start = false;
    s_pending_start_source = NULL;
    s_pending_start_deadline_tick = 0;
    s_pending_start_next_retry_tick = 0;
}

static void voice_recording_control_cancel_pending_start(const char *detail)
{
    if (!s_pending_start) {
        return;
    }

    const char *source = s_pending_start_source != NULL ? s_pending_start_source : "unknown";
    voice_recording_control_reset_pending_start();
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    ESP_LOGI(TAG, "recording pending start canceled source=%s detail=%s", source, detail);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_CANCEL,
        detail,
        source,
        ESP_OK,
        false);
    voice_recording_control_log_device_status("ready", detail);
}

static void voice_recording_control_schedule_pending_start(const char *source)
{
    TickType_t now = xTaskGetTickCount();
    s_pending_start = true;
    s_pending_start_source = source;
    s_pending_start_deadline_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS);
    s_pending_start_next_retry_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS);

    power_manager_record_activity("voice_recording_wait_transport");
    (void)ble_hid_gap_request_active_connection();
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        true);
    ESP_LOGW(
        TAG,
        "recording start pending source=%s timeout_ms=%u reason=audio_transport_not_ready",
        source,
        VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_PENDING_START,
        "pending_start",
        source,
        ESP_ERR_INVALID_STATE,
        true);
    voice_recording_control_log_device_status("ready", "recording_waiting_for_ble_audio");
}

static void voice_recording_control_timeout_pending_start(esp_err_t reason)
{
    const char *source = s_pending_start_source != NULL ? s_pending_start_source : "unknown";
    voice_recording_control_reset_pending_start();
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    ESP_LOGW(
        TAG,
        "recording pending start timed out source=%s timeout_ms=%u reason=%s",
        source,
        VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS,
        esp_err_to_name(reason));
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_PENDING_TIMEOUT,
        "pending_timeout",
        source,
        reason,
        true);
    voice_recording_control_log_device_error("ready", "recording_start_transport_timeout", reason);
    diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_REJECTED, DIAG_SEV_WARN,
             voice_recording_source_code(source), (uint32_t)reason, (uint32_t)s_state, 1);
}

static esp_err_t voice_recording_control_enter_recording(const char *source, bool log_rejection, bool request_reconnect)
{
    power_manager_record_activity("voice_recording_start");
    (void)ble_hid_gap_request_active_connection();
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        true);

    esp_err_t ret = audio_capture_session_begin();
    if (ret != ESP_OK) {
        if (request_reconnect) {
            (void)ble_hid_gap_request_reconnect();
        }
        voice_recording_control_clear_power_blockers();
        (void)voice_key_input_set_recording_output(false);
        if (log_rejection) {
            ESP_LOGW(TAG, "recording start rejected source=%s: %s", source, esp_err_to_name(ret));
            voice_recording_control_log_flow(
                VOICE_RECORDING_FLOW_START_REJECTED,
                "start_rejected",
                source,
                ret,
                true);
            voice_recording_control_log_device_error("error", "recording_start_rejected", ret);
            diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_REJECTED, DIAG_SEV_WARN,
                     voice_recording_source_code(source), (uint32_t)ret, (uint32_t)s_state, 0);
        }
        return ret;
    }

    voice_recording_control_reset_pending_start();
    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = source;
    s_state = VOICE_RECORDING_STATE_RECORDING;
    (void)voice_key_input_set_recording_output(true);
    s_session_count++;
    ESP_LOGI(TAG, "recording start source=%s", source);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_START_OK,
        "start_ok",
        source,
        ESP_OK,
        false);
    voice_recording_control_log_device_status("recording", "capture_active");
    diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
             1, voice_recording_source_code(source), s_session_count, 0);
    return ESP_OK;
}

static esp_err_t voice_recording_control_exit_recording(const char *source)
{
    power_manager_record_activity("voice_recording_stop");
    esp_err_t ret = audio_capture_session_stop();
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_active_session_source = NULL;
            voice_recording_control_clear_power_blockers();
            (void)voice_key_input_set_recording_output(false);
        }
        ESP_LOGW(TAG, "recording stop rejected source=%s: %s", source, esp_err_to_name(ret));
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_STOP_REJECTED,
            "stop_rejected",
            source,
            ret,
            true);
        voice_recording_control_log_device_error("error", "recording_stop_rejected", ret);
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_REJECTED, DIAG_SEV_WARN,
                 voice_recording_source_code(source), (uint32_t)ret, (uint32_t)s_state, 0);
        return ret;
    }

    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_state = VOICE_RECORDING_STATE_TRANSFERRING;
    (void)voice_key_input_set_recording_output(false);
    ESP_LOGI(TAG, "recording stop source=%s", source);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_STOP_REQUESTED,
        "stop_requested",
        source,
        ESP_OK,
        false);
    voice_recording_control_log_device_status("transferring", "audio_session_finishing");
    diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
             2, voice_recording_source_code(source), s_session_count, 0);
    return ESP_OK;
}

static void voice_recording_control_toggle(const char *source)
{
    if (s_cancel_pending) {
        ESP_LOGW(TAG, "recording toggle ignored source=%s: cancel pending", source);
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            "toggle_ignored_cancel_pending",
            source,
            ESP_ERR_INVALID_STATE,
            true);
        return;
    }

    if (s_pending_start) {
        power_manager_record_activity("voice_recording_pending_start");
        ESP_LOGI(
            TAG,
            "recording toggle ignored source=%s: pending start source=%s",
            source,
            s_pending_start_source != NULL ? s_pending_start_source : "unknown");
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            "toggle_ignored_pending_start",
            source,
            ESP_ERR_INVALID_STATE,
            false);
        voice_recording_control_log_device_status("ready", "recording_waiting_for_ble_audio");
        return;
    }

    if (s_state == VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_TOGGLE_START,
            "toggle_start",
            source,
            ESP_OK,
            false);
        esp_err_t ret = voice_recording_control_enter_recording(source, true, true);
        if (ret == ESP_ERR_INVALID_STATE && !audio_capture_session_is_active()) {
            voice_recording_control_schedule_pending_start(source);
        }
    } else {
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_TOGGLE_STOP,
            "toggle_stop",
            source,
            ESP_OK,
            false);
        voice_recording_control_exit_recording(source);
    }
}

static void voice_recording_control_cancel(const char *source)
{
    if (s_pending_start) {
        power_manager_record_activity("voice_recording_cancel_pending_start");
        voice_recording_control_cancel_pending_start("recording_pending_start_canceled");
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 3, voice_recording_source_code(source), s_session_count, 0);
        return;
    }

    power_manager_record_activity("voice_recording_cancel");
    esp_err_t ret = audio_capture_session_cancel();
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_active_session_source = NULL;
            voice_recording_control_clear_power_blockers();
        }
        ESP_LOGW(TAG, "recording cancel rejected source=%s: %s", source, esp_err_to_name(ret));
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_CANCEL,
            "cancel_rejected",
            source,
            ret,
            true);
        voice_recording_control_log_device_error("error", "recording_cancel_rejected", ret);
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_REJECTED, DIAG_SEV_WARN,
                 voice_recording_source_code(source), (uint32_t)ret, (uint32_t)s_state, 0);
        return;
    }

    s_cancel_pending = true;
    s_cancel_source = source;
    if (!audio_capture_session_is_active()) {
        s_cancel_pending = false;
        s_cancel_source = NULL;
        s_state = VOICE_RECORDING_STATE_IDLE;
        s_active_session_source = NULL;
        voice_recording_control_clear_power_blockers();
        (void)voice_key_input_set_recording_output(false);
        ESP_LOGI(TAG, "recording cancel source=%s", source);
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_CANCEL,
            "cancel_complete",
            source,
            ESP_OK,
            false);
        voice_recording_control_log_device_status("ready", "recording_canceled");
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 3, voice_recording_source_code(source), s_session_count, 0);
        return;
    }

    ESP_LOGI(TAG, "recording cancel requested source=%s", source);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_CANCEL,
        "cancel_requested",
        source,
        ESP_OK,
        false);
}

static void voice_recording_control_recovery(const char *source)
{
    power_manager_record_activity("voice_recording_recovery");
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_RECOVERY,
        "recovery_requested",
        source,
        ESP_OK,
        true);
    voice_recording_control_cancel_pending_start("recovery_cleared_pending_start");
    (void)voice_key_input_set_recording_output(false);
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
        true);
    voice_recording_state_t previous_state = s_state;
    s_state = VOICE_RECORDING_STATE_RECOVERY;
    ESP_LOGW(TAG, "recovery requested source=%s", source);
    voice_recording_control_log_device_status("recovery", "forget_pairing_and_clear_session");

    if (audio_capture_session_is_active()) {
        esp_err_t cancel_ret = audio_capture_session_cancel();
        if (cancel_ret == ESP_OK) {
            ESP_LOGW(TAG, "recovery canceled active recording session source=%s", source);
        } else {
            ESP_LOGW(TAG, "recovery session cancel failed source=%s: %s", source, esp_err_to_name(cancel_ret));
            voice_recording_control_log_device_error("error", "recovery_cancel_session_failed", cancel_ret);
        }
    }

    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = NULL;
    esp_err_t ret = ble_hid_gap_forget_bonds_and_repair();
    if (ret != ESP_OK) {
        voice_recording_control_log_device_error("error", "recovery_pairing_reset_failed", ret);
        s_state = previous_state == VOICE_RECORDING_STATE_RECORDING ? VOICE_RECORDING_STATE_RECORDING : VOICE_RECORDING_STATE_IDLE;
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
            false);
        return;
    }

    s_state = VOICE_RECORDING_STATE_IDLE;
    voice_recording_control_clear_power_blockers();
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
        false);
    voice_recording_control_log_device_status("ready", "recovery_complete_pair_again");
}

static void voice_recording_control_poll_pending_start(void)
{
    if (!s_pending_start) {
        return;
    }

    if (s_state != VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_reset_pending_start();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (voice_recording_control_tick_reached(now, s_pending_start_deadline_tick)) {
        voice_recording_control_timeout_pending_start(ESP_ERR_TIMEOUT);
        return;
    }

    if (!voice_recording_control_tick_reached(now, s_pending_start_next_retry_tick)) {
        return;
    }
    s_pending_start_next_retry_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS);

    if (!ble_audio_stream_is_ready()) {
        return;
    }

    const char *source = s_pending_start_source != NULL ? s_pending_start_source : "pending";
    ESP_LOGI(TAG, "recording pending start transport ready source=%s", source);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_PENDING_READY,
        "pending_ready",
        source,
        ESP_OK,
        false);
    esp_err_t ret = voice_recording_control_enter_recording(source, false, false);
    if (ret == ESP_OK) {
        return;
    }

    if (ret != ESP_ERR_INVALID_STATE) {
        voice_recording_control_timeout_pending_start(ret);
        return;
    }

    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        true);
}

static void voice_recording_control_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("voice_recording_control_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (voice_key_input_take_toggle_event()) {
            voice_recording_control_toggle(voice_key_input_get_active_source());
        }

        if (voice_key_input_take_recovery_event()) {
            const char *source = voice_key_input_get_active_source();
            voice_recording_control_recovery(source != NULL ? source : "voice_key_hold");
        }

        if ((s_state == VOICE_RECORDING_STATE_RECORDING || s_state == VOICE_RECORDING_STATE_TRANSFERRING) &&
            !audio_capture_session_is_active()) {
            voice_recording_state_t finished_state = s_state;
            if (s_cancel_pending) {
                const char *source = s_cancel_source != NULL ? s_cancel_source : "unknown";
                s_cancel_pending = false;
                s_cancel_source = NULL;
                s_state = VOICE_RECORDING_STATE_IDLE;
                s_active_session_source = NULL;
                voice_recording_control_clear_power_blockers();
                (void)voice_key_input_set_recording_output(false);
                ESP_LOGI(TAG, "recording cancel source=%s", source);
                voice_recording_control_log_flow(
                    VOICE_RECORDING_FLOW_CANCEL,
                    "cancel_complete",
                    source,
                    ESP_OK,
                    false);
                voice_recording_control_log_device_status("ready", "recording_canceled");
                diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                         3, voice_recording_source_code(source), s_session_count, 0);
                continue;
            }

            const char *source = s_active_session_source != NULL ? s_active_session_source : "session";
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_active_session_source = NULL;
            voice_recording_control_clear_power_blockers();
            (void)voice_key_input_set_recording_output(false);
            if (finished_state == VOICE_RECORDING_STATE_TRANSFERRING) {
                ESP_LOGI(TAG, "recording session finished");
                voice_recording_control_log_flow(
                    VOICE_RECORDING_FLOW_SESSION_FINISHED,
                    "session_finished",
                    source,
                    ESP_OK,
                    false);
                voice_recording_control_log_device_status("ready", "recording_session_finished");
            } else {
                ESP_LOGW(TAG, "recording session ended without stop request");
                voice_recording_control_log_flow(
                    VOICE_RECORDING_FLOW_SESSION_ABORTED,
                    "session_aborted_without_stop",
                    source,
                    ESP_ERR_INVALID_STATE,
                    true);
                voice_recording_control_log_device_error("ready", "recording_session_aborted", ESP_ERR_INVALID_STATE);
            }
        }

        voice_recording_control_poll_pending_start();
        vTaskDelay(pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_SESSION_CHECK_MS));
    }
}

uint32_t voice_recording_control_get_session_count(void)
{
    return s_session_count;
}

esp_err_t voice_recording_control_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t audio_ret = audio_capture_start();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "audio capture start failed; keeping recovery/status path alive: %s", esp_err_to_name(audio_ret));
        voice_recording_control_log_device_error("error", "audio_capture_start_failed", audio_ret);
    }

    esp_err_t key_ret = voice_key_input_start();
    if (key_ret != ESP_OK) {
        ESP_LOGW(TAG, "voice key input start failed; USB recovery remains available: %s", esp_err_to_name(key_ret));
        voice_recording_control_log_device_error("error", "voice_key_input_start_failed", key_ret);
    }

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
    ESP_LOGI(TAG, "voice recording control ready: source=%s toggle start/stop", voice_key_input_get_active_source());
    if (audio_ret == ESP_OK && key_ret == ESP_OK) {
        voice_recording_control_log_device_status("ready", "voice_recording_control_started");
        return ESP_OK;
    }

    if (key_ret != ESP_OK) {
        voice_recording_control_log_device_error("error", "voice_recording_control_input_failed", key_ret);
        return key_ret;
    }

    voice_recording_control_log_device_error("degraded", "voice_recording_control_audio_degraded", audio_ret);
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
            } else if (strcmp(action, "RECOVERY") == 0 || strcmp(action, "RESET") == 0 || strcmp(action, "FORGET") == 0) {
                voice_recording_control_recovery("usb");
            } else {
                ESP_LOGW(TAG, "drop control command: %s", s_usb_command_buffer);
                voice_recording_control_log_device_error("error", "unknown_usb_control_command", ESP_ERR_INVALID_ARG);
            }
            return true;
        }

        ESP_LOGW(TAG, "drop control command: %s", s_usb_command_buffer);
        voice_recording_control_log_device_error("error", "unknown_usb_control_prefix", ESP_ERR_INVALID_ARG);
        return true;
    }

    if (s_usb_command_length + 1 >= sizeof(s_usb_command_buffer)) {
        s_usb_command_active = false;
        s_usb_command_length = 0;
        memset(s_usb_command_buffer, 0, sizeof(s_usb_command_buffer));
        ESP_LOGW(TAG, "drop control command: too long");
        voice_recording_control_log_device_error("error", "usb_control_command_too_long", ESP_ERR_INVALID_SIZE);
        return true;
    }

    s_usb_command_buffer[s_usb_command_length++] = (char)input_char;
    return true;
}
