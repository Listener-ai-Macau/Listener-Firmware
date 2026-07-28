#include "voice_recording_control.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "diag_log.h"

#include "audio_capture.h"
#include "ble_audio_stream.h"
#include "ble_hid_gap.h"
#include "denzic_voice_activation_v1.h"
#include "device_settings.h"
#include "power_manager.h"
#include "status_led.h"
#include "voice_key_input.h"
#include "watchdog_platform.h"

#define VOICE_RECORDING_CONTROL_PREFIX_CHAR '~'
#define VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES 32
#define VOICE_RECORDING_CONTROL_VREC_PREFIX "VREC:"
#define VOICE_RECORDING_CONTROL_SESSION_CHECK_MS 50
#define VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS 12000
#define VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS 250
#define VOICE_RECORDING_CONTROL_HOST_CLEANUP_TOGGLE_GUARD_MS 1500
#define VOICE_RECORDING_CONTROL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define VOICE_RECORDING_CONTROL_VAD_QUEUE_LENGTH 24

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

typedef enum {
    VOICE_RECORDING_EVENT_TOGGLE = 0,
    VOICE_RECORDING_EVENT_STOP,
    VOICE_RECORDING_EVENT_CANCEL,
    VOICE_RECORDING_EVENT_RECOVERY,
    VOICE_RECORDING_EVENT_PENDING_READY,
    VOICE_RECORDING_EVENT_PENDING_TIMEOUT,
    VOICE_RECORDING_EVENT_SESSION_INACTIVE,
    VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY,
} voice_recording_control_event_t;

typedef enum {
    VOICE_RECORDING_SOURCE_OTHER = 0,
    VOICE_RECORDING_SOURCE_USER_START_INTENT,
    VOICE_RECORDING_SOURCE_HOST_CONTROL,
} voice_recording_control_source_class_t;

typedef enum {
    VOICE_RECORDING_EFFECT_NONE = 0,
    VOICE_RECORDING_EFFECT_IGNORE,
    VOICE_RECORDING_EFFECT_START_RECORDING,
    VOICE_RECORDING_EFFECT_STOP_RECORDING,
    VOICE_RECORDING_EFFECT_STOP_CLEANUP,
    VOICE_RECORDING_EFFECT_CANCEL_RECORDING,
    VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
    VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START,
    VOICE_RECORDING_EFFECT_TIMEOUT_PENDING_START,
    VOICE_RECORDING_EFFECT_RESET_PENDING_START,
    VOICE_RECORDING_EFFECT_FINISH_CANCEL,
    VOICE_RECORDING_EFFECT_FINISH_TRANSFER,
    VOICE_RECORDING_EFFECT_FINISH_ABORT,
    VOICE_RECORDING_EFFECT_RECOVERY,
} voice_recording_control_effect_t;

typedef struct {
    voice_recording_state_t state;
    voice_recording_control_source_class_t source_class;
    bool pending_start;
    bool pending_start_from_user;
    bool cancel_pending;
    bool audio_active;
    bool ble_ready;
    bool host_cleanup_guard_active;
} voice_recording_control_snapshot_t;

typedef struct {
    voice_recording_control_effect_t effect;
    voice_recording_state_t next_state;
    voice_recording_flow_stage_t flow_stage;
    esp_err_t result;
    bool warn;
    const char *detail;
    const char *activity;
    const char *pending_reason;
    const char *pending_detail;
} voice_recording_control_decision_t;

typedef struct {
    const char *id;
    voice_recording_state_t state;
    voice_recording_control_event_t event;
    voice_recording_control_source_class_t source_class;
    bool pending_start;
    bool pending_start_from_user;
    bool cancel_pending;
    bool audio_active;
    bool ble_ready;
    bool host_cleanup_guard_active;
    voice_recording_control_effect_t effect;
    const char *detail;
} voice_recording_control_transition_case_t;

typedef struct {
    bool speech_detected;
    uint32_t elapsed_ms;
} voice_recording_control_vad_event_t;

static const char *TAG = "voice_rec_ctrl";

static bool s_started;
static TaskHandle_t s_task_handle;
static SemaphoreHandle_t s_state_mutex;
static bool s_usb_command_active;
static size_t s_usb_command_length;
static char s_usb_command_buffer[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
static voice_recording_state_t s_state = VOICE_RECORDING_STATE_IDLE;
static bool s_cancel_pending;
static const char *s_cancel_source;
static bool s_pending_start;
static const char *s_pending_start_source;
static const char *s_pending_start_reason;
static const char *s_pending_start_detail;
static TickType_t s_pending_start_deadline_tick;
static TickType_t s_pending_start_next_retry_tick;
static const char *s_active_session_source;
static bool s_active_session_automatic;
static bool s_active_session_visible;
static TickType_t s_host_cleanup_toggle_guard_until_tick;
static uint32_t s_session_count;
static QueueHandle_t s_vad_queue;
static bool s_voice_monitoring;
static bool s_voice_auto_start_enabled;
static bool s_voice_auto_stop_enabled;
static denzic_voice_activation_v1_machine_t s_voice_activation_machine;
static denzic_voice_activation_v1_config_t s_voice_activation_config;

static const voice_recording_control_transition_case_t VOICE_RECORDING_CONTROL_FSM_ARTIFACT[] = {
    {
        "rapid_next_start_while_transferring",
        VOICE_RECORDING_STATE_TRANSFERRING,
        VOICE_RECORDING_EVENT_TOGGLE,
        VOICE_RECORDING_SOURCE_USER_START_INTENT,
        false,
        false,
        false,
        true,
        true,
        false,
        VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START,
        "toggle_start_pending_transfer",
    },
    {
        "host_cleanup_toggle_after_abort",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_TOGGLE,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        false,
        true,
        true,
        VOICE_RECORDING_EFFECT_IGNORE,
        "host_cleanup_toggle_ignored_after_abort",
    },
    {
        "host_cleanup_clears_host_pending_start",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_TOGGLE,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        true,
        false,
        false,
        false,
        false,
        false,
        VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
        "host_cleanup_toggle_cleared_pending_start",
    },
    {
        "host_cleanup_preserves_user_pending_start",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_TOGGLE,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        true,
        true,
        false,
        false,
        false,
        false,
        VOICE_RECORDING_EFFECT_IGNORE,
        "host_cleanup_toggle_ignored_user_pending",
    },
    {
        "transport_not_ready_user_start",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY,
        VOICE_RECORDING_SOURCE_USER_START_INTENT,
        false,
        false,
        false,
        false,
        false,
        false,
        VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START,
        "recording_waiting_for_ble_audio",
    },
    {
        "transport_not_ready_host_cleanup",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        false,
        false,
        false,
        VOICE_RECORDING_EFFECT_IGNORE,
        "host_toggle_transport_not_ready_no_pending",
    },
    {
        "cancel_while_pending_start",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_CANCEL,
        VOICE_RECORDING_SOURCE_USER_START_INTENT,
        true,
        true,
        false,
        false,
        false,
        false,
        VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
        "recording_pending_start_canceled",
    },
    {
        "cancel_while_transferring",
        VOICE_RECORDING_STATE_TRANSFERRING,
        VOICE_RECORDING_EVENT_CANCEL,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        true,
        true,
        false,
        VOICE_RECORDING_EFFECT_CANCEL_RECORDING,
        "cancel_requested",
    },
    {
        "recovery_during_recording",
        VOICE_RECORDING_STATE_RECORDING,
        VOICE_RECORDING_EVENT_RECOVERY,
        VOICE_RECORDING_SOURCE_USER_START_INTENT,
        false,
        false,
        false,
        true,
        true,
        false,
        VOICE_RECORDING_EFFECT_RECOVERY,
        "recovery_requested",
    },
    {
        "audio_session_finished_after_stop",
        VOICE_RECORDING_STATE_TRANSFERRING,
        VOICE_RECORDING_EVENT_SESSION_INACTIVE,
        VOICE_RECORDING_SOURCE_OTHER,
        false,
        false,
        false,
        false,
        true,
        false,
        VOICE_RECORDING_EFFECT_FINISH_TRANSFER,
        "recording_session_finished",
    },
    {
        "audio_session_aborted_without_active_capture",
        VOICE_RECORDING_STATE_RECORDING,
        VOICE_RECORDING_EVENT_SESSION_INACTIVE,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        false,
        true,
        false,
        VOICE_RECORDING_EFFECT_FINISH_ABORT,
        "session_aborted_without_stop",
    },
    {
        "stale_stop_command",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_STOP,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        false,
        true,
        false,
        VOICE_RECORDING_EFFECT_IGNORE,
        "stop_ignored_no_active_session",
    },
    {
        "stale_cancel_command",
        VOICE_RECORDING_STATE_IDLE,
        VOICE_RECORDING_EVENT_CANCEL,
        VOICE_RECORDING_SOURCE_HOST_CONTROL,
        false,
        false,
        false,
        false,
        true,
        false,
        VOICE_RECORDING_EFFECT_IGNORE,
        "cancel_ignored_no_active_session",
    },
};

static esp_err_t voice_recording_control_activate_automatic_session(const char *source);

static bool voice_recording_control_lock(void)
{
    return s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE;
}

static void voice_recording_control_unlock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
}

static voice_recording_control_source_class_t voice_recording_control_classify_source(const char *source)
{
    if (source == NULL) {
        return VOICE_RECORDING_SOURCE_OTHER;
    }
    if (strncmp(source, "voice", strlen("voice")) == 0 ||
        strncmp(source, "key1", strlen("key1")) == 0 ||
        strncmp(source, "ec11", strlen("ec11")) == 0) {
        return VOICE_RECORDING_SOURCE_USER_START_INTENT;
    }
    if (strcmp(source, "usb") == 0 || strcmp(source, "ble_audio_control") == 0) {
        return VOICE_RECORDING_SOURCE_HOST_CONTROL;
    }
    return VOICE_RECORDING_SOURCE_OTHER;
}

static bool voice_recording_control_source_is_user_start_intent(const char *source)
{
    return voice_recording_control_classify_source(source) ==
           VOICE_RECORDING_SOURCE_USER_START_INTENT;
}

static bool voice_recording_control_source_is_host_control(const char *source)
{
    return voice_recording_control_classify_source(source) ==
           VOICE_RECORDING_SOURCE_HOST_CONTROL;
}

static bool voice_recording_control_source_is_ble_audio_control(const char *source)
{
    return source != NULL && strcmp(source, "ble_audio_control") == 0;
}

static void voice_recording_control_note_ble_type_processing_activity(
    const char *source,
    const char *reason)
{
    if (voice_recording_control_source_is_ble_audio_control(source)) {
        ble_audio_stream_note_type_activity(reason);
    }
}

static uint32_t voice_recording_source_code(const char *source)
{
    if (source == NULL) {
        return 0;
    }
    if (voice_recording_control_source_is_user_start_intent(source)) {
        return 1;
    }
    if (strcmp(source, "usb") == 0) {
        return 2;
    }
    if (strcmp(source, "ble_audio_control") == 0) {
        return 3;
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

static void voice_recording_control_arm_host_cleanup_toggle_guard(void)
{
    s_host_cleanup_toggle_guard_until_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_HOST_CLEANUP_TOGGLE_GUARD_MS);
}

static bool voice_recording_control_host_cleanup_toggle_guard_active(const char *source)
{
    return voice_recording_control_source_is_host_control(source) &&
           !voice_recording_control_tick_reached(
               xTaskGetTickCount(),
               s_host_cleanup_toggle_guard_until_tick);
}

static voice_recording_control_snapshot_t voice_recording_control_make_snapshot(const char *source)
{
    return (voice_recording_control_snapshot_t) {
        .state = s_state,
        .source_class = voice_recording_control_classify_source(source),
        .pending_start = s_pending_start,
        .pending_start_from_user =
            voice_recording_control_source_is_user_start_intent(s_pending_start_source),
        .cancel_pending = s_cancel_pending,
        .audio_active = audio_capture_session_is_active(),
        .ble_ready = ble_audio_stream_is_ready(),
        .host_cleanup_guard_active =
            voice_recording_control_host_cleanup_toggle_guard_active(source),
    };
}

static voice_recording_control_decision_t voice_recording_control_make_decision(
    voice_recording_control_effect_t effect,
    voice_recording_state_t next_state,
    voice_recording_flow_stage_t flow_stage,
    esp_err_t result,
    bool warn,
    const char *detail,
    const char *activity,
    const char *pending_reason,
    const char *pending_detail)
{
    return (voice_recording_control_decision_t) {
        .effect = effect,
        .next_state = next_state,
        .flow_stage = flow_stage,
        .result = result,
        .warn = warn,
        .detail = detail,
        .activity = activity,
        .pending_reason = pending_reason,
        .pending_detail = pending_detail,
    };
}

static voice_recording_control_decision_t voice_recording_control_decide_transition(
    voice_recording_control_event_t event,
    const voice_recording_control_snapshot_t *snapshot)
{
    voice_recording_state_t state = snapshot->state;

    if (event == VOICE_RECORDING_EVENT_RECOVERY) {
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_RECOVERY,
            VOICE_RECORDING_STATE_RECOVERY,
            VOICE_RECORDING_FLOW_RECOVERY,
            ESP_OK,
            true,
            "recovery_requested",
            "voice_recording_recovery",
            NULL,
            NULL);
    }

    if (snapshot->cancel_pending && event == VOICE_RECORDING_EVENT_TOGGLE) {
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_IGNORE,
            state,
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            ESP_ERR_INVALID_STATE,
            true,
            "toggle_ignored_cancel_pending",
            NULL,
            NULL,
            NULL);
    }

    if (snapshot->pending_start) {
        if (event == VOICE_RECORDING_EVENT_CANCEL) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_CANCEL,
                ESP_OK,
                false,
                "recording_pending_start_canceled",
                "voice_recording_cancel_pending_start",
                NULL,
                NULL);
        }
        if (event == VOICE_RECORDING_EVENT_STOP) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_STOP_REQUESTED,
                ESP_OK,
                false,
                "stop_cleared_pending_start",
                "voice_recording_stop_pending_start",
                NULL,
                NULL);
        }
        if (event == VOICE_RECORDING_EVENT_TOGGLE &&
            snapshot->source_class == VOICE_RECORDING_SOURCE_HOST_CONTROL) {
            if (!snapshot->pending_start_from_user) {
                return voice_recording_control_make_decision(
                    VOICE_RECORDING_EFFECT_CLEAR_PENDING_START,
                    VOICE_RECORDING_STATE_IDLE,
                    VOICE_RECORDING_FLOW_CANCEL,
                    ESP_OK,
                    false,
                    "host_cleanup_toggle_cleared_pending_start",
                    "voice_recording_host_cleanup_pending_start",
                    NULL,
                    NULL);
            }
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_IGNORE,
                state,
                VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                ESP_OK,
                false,
                "host_cleanup_toggle_ignored_user_pending",
                "voice_recording_host_cleanup_user_pending",
                NULL,
                NULL);
        }
        if (event == VOICE_RECORDING_EVENT_TOGGLE) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_IGNORE,
                state,
                VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                ESP_ERR_INVALID_STATE,
                false,
                "toggle_ignored_pending_start",
                "voice_recording_pending_start",
                NULL,
                NULL);
        }
        if (event == VOICE_RECORDING_EVENT_PENDING_TIMEOUT) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_TIMEOUT_PENDING_START,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_PENDING_TIMEOUT,
                ESP_ERR_TIMEOUT,
                true,
                "pending_timeout",
                NULL,
                NULL,
                NULL);
        }
    }

    if (event == VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY) {
        if (snapshot->source_class == VOICE_RECORDING_SOURCE_USER_START_INTENT) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START,
                state,
                VOICE_RECORDING_FLOW_PENDING_START,
                ESP_ERR_INVALID_STATE,
                true,
                "pending_start",
                NULL,
                "audio_transport_not_ready",
                "recording_waiting_for_ble_audio");
        }
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_IGNORE,
            state,
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            ESP_ERR_INVALID_STATE,
            true,
            "host_toggle_transport_not_ready_no_pending",
            NULL,
            NULL,
            NULL);
    }

    if (event == VOICE_RECORDING_EVENT_SESSION_INACTIVE) {
        if (snapshot->audio_active) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_IGNORE,
                state,
                VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                ESP_OK,
                false,
                "session_still_active",
                NULL,
                NULL,
                NULL);
        }
        if (snapshot->cancel_pending) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_FINISH_CANCEL,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_CANCEL,
                ESP_OK,
                false,
                "cancel_complete",
                NULL,
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_TRANSFERRING) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_FINISH_TRANSFER,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_SESSION_FINISHED,
                ESP_OK,
                false,
                "session_finished",
                NULL,
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_RECORDING) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_FINISH_ABORT,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_SESSION_ABORTED,
                ESP_ERR_INVALID_STATE,
                true,
                "session_aborted_without_stop",
                NULL,
                NULL,
                NULL);
        }
    }

    if (event == VOICE_RECORDING_EVENT_PENDING_READY) {
        if (state == VOICE_RECORDING_STATE_TRANSFERRING) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_IGNORE,
                state,
                VOICE_RECORDING_FLOW_PENDING_START,
                ESP_OK,
                false,
                "pending_waiting_for_transfer",
                NULL,
                NULL,
                NULL);
        }
        if (state != VOICE_RECORDING_STATE_IDLE) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_RESET_PENDING_START,
                state,
                VOICE_RECORDING_FLOW_PENDING_START,
                ESP_ERR_INVALID_STATE,
                false,
                "pending_reset_non_idle_state",
                NULL,
                NULL,
                NULL);
        }
        if (!snapshot->ble_ready) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_IGNORE,
                state,
                VOICE_RECORDING_FLOW_PENDING_START,
                ESP_ERR_INVALID_STATE,
                false,
                "pending_waiting_for_ble_audio",
                NULL,
                NULL,
                NULL);
        }
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_START_RECORDING,
            VOICE_RECORDING_STATE_RECORDING,
            VOICE_RECORDING_FLOW_PENDING_READY,
            ESP_OK,
            false,
            "pending_ready",
            NULL,
            NULL,
            NULL);
    }

    if (event == VOICE_RECORDING_EVENT_TOGGLE) {
        if (state == VOICE_RECORDING_STATE_IDLE) {
            if (snapshot->host_cleanup_guard_active) {
                return voice_recording_control_make_decision(
                    VOICE_RECORDING_EFFECT_IGNORE,
                    state,
                    VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                    ESP_OK,
                    false,
                    "host_cleanup_toggle_ignored_after_abort",
                    "voice_recording_host_cleanup_toggle",
                    NULL,
                    NULL);
            }
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_START_RECORDING,
                VOICE_RECORDING_STATE_RECORDING,
                VOICE_RECORDING_FLOW_TOGGLE_START,
                ESP_OK,
                false,
                "toggle_start",
                NULL,
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_RECORDING) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_STOP_RECORDING,
                VOICE_RECORDING_STATE_TRANSFERRING,
                VOICE_RECORDING_FLOW_TOGGLE_STOP,
                ESP_OK,
                false,
                "toggle_stop",
                NULL,
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_TRANSFERRING) {
            if (snapshot->source_class == VOICE_RECORDING_SOURCE_USER_START_INTENT) {
                return voice_recording_control_make_decision(
                    VOICE_RECORDING_EFFECT_SCHEDULE_PENDING_START,
                    state,
                    VOICE_RECORDING_FLOW_TOGGLE_START,
                    ESP_OK,
                    false,
                    "toggle_start_pending_transfer",
                    "voice_recording_pending_transfer_start",
                    "audio_session_transferring",
                    "recording_waiting_for_previous_session");
            }
            return voice_recording_control_make_decision(
                snapshot->audio_active ?
                    VOICE_RECORDING_EFFECT_STOP_RECORDING :
                    VOICE_RECORDING_EFFECT_STOP_CLEANUP,
                snapshot->audio_active ? VOICE_RECORDING_STATE_TRANSFERRING : VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_STOP_REQUESTED,
                ESP_OK,
                false,
                snapshot->audio_active ? "stop_requested" : "host_cleanup_stop_completed",
                NULL,
                NULL,
                NULL);
        }
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_IGNORE,
            state,
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            ESP_ERR_INVALID_STATE,
            true,
            snapshot->source_class == VOICE_RECORDING_SOURCE_HOST_CONTROL ?
                "host_cleanup_toggle_ignored_recovery" :
                "toggle_ignored_recovery",
            NULL,
            NULL,
            NULL);
    }

    if (event == VOICE_RECORDING_EVENT_STOP) {
        if (state == VOICE_RECORDING_STATE_TRANSFERRING && !snapshot->audio_active) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_STOP_CLEANUP,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                ESP_OK,
                false,
                "host_cleanup_stop_completed",
                "voice_recording_stop_cleanup",
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_RECORDING ||
            state == VOICE_RECORDING_STATE_TRANSFERRING) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_STOP_RECORDING,
                VOICE_RECORDING_STATE_TRANSFERRING,
                VOICE_RECORDING_FLOW_STOP_REQUESTED,
                ESP_OK,
                false,
                "stop_requested",
                NULL,
                NULL,
                NULL);
        }
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_IGNORE,
            state,
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            ESP_OK,
            false,
            "stop_ignored_no_active_session",
            "voice_recording_stop_cleanup",
            NULL,
            NULL);
    }

    if (event == VOICE_RECORDING_EVENT_CANCEL) {
        if (state == VOICE_RECORDING_STATE_RECORDING ||
            (state == VOICE_RECORDING_STATE_TRANSFERRING && snapshot->audio_active)) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_CANCEL_RECORDING,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_CANCEL,
                ESP_OK,
                false,
                "cancel_requested",
                "voice_recording_cancel",
                NULL,
                NULL);
        }
        if (state == VOICE_RECORDING_STATE_TRANSFERRING && !snapshot->audio_active) {
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_STOP_CLEANUP,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_CANCEL,
                ESP_OK,
                false,
                "cancel_ignored_transfer_complete",
                "voice_recording_cancel_cleanup",
                NULL,
                NULL);
        }
        return voice_recording_control_make_decision(
            VOICE_RECORDING_EFFECT_IGNORE,
            state,
            VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
            ESP_OK,
            false,
            "cancel_ignored_no_active_session",
            "voice_recording_stale_cancel",
            NULL,
            NULL);
    }

    return voice_recording_control_make_decision(
        VOICE_RECORDING_EFFECT_IGNORE,
        state,
        VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
        ESP_OK,
        false,
        "event_ignored",
        NULL,
        NULL,
        NULL);
}

static void voice_recording_control_reset_pending_start(void)
{
    s_pending_start = false;
    s_pending_start_source = NULL;
    s_pending_start_reason = NULL;
    s_pending_start_detail = NULL;
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
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_REC);
    voice_recording_control_log_device_status("ready", detail);
}

static void voice_recording_control_log_toggle_ignored(
    const char *source,
    const char *detail,
    esp_err_t result,
    bool warn)
{
    ESP_LOGI(TAG, "recording toggle ignored source=%s detail=%s", source, detail);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
        detail,
        source,
        result,
        warn);
}

static void voice_recording_control_schedule_pending_start(
    const char *source,
    const char *reason,
    const char *detail)
{
    TickType_t now = xTaskGetTickCount();
    s_pending_start = true;
    s_pending_start_source = source;
    s_pending_start_reason = reason != NULL ? reason : "audio_transport_not_ready";
    s_pending_start_detail = detail != NULL ? detail : "recording_waiting_for_ble_audio";
    s_pending_start_deadline_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS);
    s_pending_start_next_retry_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS);

    power_manager_record_activity("voice_recording_wait_transport");
    (void)ble_hid_gap_request_active_connection();
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        true);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_REC);
    ESP_LOGW(
        TAG,
        "recording start pending source=%s timeout_ms=%u reason=%s",
        source,
        VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS,
        s_pending_start_reason);
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_PENDING_START,
        "pending_start",
        source,
        ESP_ERR_INVALID_STATE,
        true);
    voice_recording_control_log_device_status("ready", s_pending_start_detail);
}

static void voice_recording_control_timeout_pending_start(esp_err_t reason)
{
    const char *source = s_pending_start_source != NULL ? s_pending_start_source : "unknown";
    voice_recording_control_reset_pending_start();
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
    status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "recording_start_transport_timeout");
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

static esp_err_t voice_recording_control_enter_recording(
    const char *source,
    bool log_rejection,
    bool request_reconnect,
    uint32_t pre_roll_ms)
{
    power_manager_record_activity("voice_recording_start");
    (void)ble_hid_gap_request_active_connection();
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
        true);

    esp_err_t ret = pre_roll_ms > 0u
        ? audio_capture_session_begin_with_preroll(pre_roll_ms)
        : audio_capture_session_begin();
    if (ret != ESP_OK) {
        const bool audio_unavailable =
            ret == ESP_ERR_NOT_SUPPORTED && !audio_capture_is_available();
        const char *detail = audio_unavailable
            ? "recording_start_audio_unavailable"
            : "recording_start_rejected";
        const char *reason = audio_unavailable
            ? audio_capture_get_unavailable_reason()
            : NULL;
        if (request_reconnect) {
            (void)ble_hid_gap_request_reconnect();
        }
        voice_recording_control_clear_power_blockers();
        (void)voice_key_input_set_recording_output(false);
        bool transport_pending = ret == ESP_ERR_INVALID_STATE && !ble_audio_stream_is_ready();
        bool suppress_retry_led_error = ret == ESP_ERR_INVALID_STATE && !log_rejection;
        if (!transport_pending && !suppress_retry_led_error) {
            status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, detail);
        }
        if (log_rejection) {
            if (reason != NULL) {
                ESP_LOGW(TAG, "recording start rejected source=%s: %s detail=%s", source, esp_err_to_name(ret), reason);
            } else {
                ESP_LOGW(TAG, "recording start rejected source=%s: %s", source, esp_err_to_name(ret));
            }
            voice_recording_control_log_flow(
                VOICE_RECORDING_FLOW_START_REJECTED,
                "start_rejected",
                source,
                ret,
                true);
            voice_recording_control_log_device_error("error", detail, ret);
            diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_REJECTED, DIAG_SEV_WARN,
                     voice_recording_source_code(source), (uint32_t)ret, (uint32_t)s_state, 0);
        }
        return ret;
    }

    voice_recording_control_reset_pending_start();
    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = source;
    s_active_session_automatic = pre_roll_ms > 0u;
    s_active_session_visible = !s_active_session_automatic;
    s_state = VOICE_RECORDING_STATE_RECORDING;
    (void)voice_key_input_set_recording_output(s_active_session_visible);
    s_session_count++;
    status_led_set_recording(
        s_active_session_visible,
        s_active_session_visible
            ? STATUS_LED_REC_SOURCE_DEVICE_MIC
            : STATUS_LED_REC_SOURCE_NONE);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_REC);
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

static esp_err_t voice_recording_control_exit_recording_with_origin(
    const char *source,
    audio_capture_stop_origin_t origin)
{
    power_manager_record_activity("voice_recording_stop");
    bool processing_feedback_allowed =
        !s_active_session_automatic || s_active_session_visible;
    esp_err_t ret = audio_capture_session_stop_with_origin(origin);
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_active_session_source = NULL;
            s_active_session_automatic = false;
            s_active_session_visible = false;
            voice_recording_control_clear_power_blockers();
            (void)voice_key_input_set_recording_output(false);
        }
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "recording_stop_rejected");
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
    s_active_session_visible = false;
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    if (processing_feedback_allowed) {
        status_led_set_processing(true, "recording_stop_processing_start");
    } else {
        ESP_LOGI(
            TAG,
            "hidden automatic candidate stopped without processing feedback source=%s",
            source);
    }
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

static esp_err_t voice_recording_control_exit_recording(const char *source)
{
    return voice_recording_control_exit_recording_with_origin(
        source,
        AUDIO_CAPTURE_STOP_ORIGIN_USER);
}

static void voice_recording_control_complete_transfer_cleanup(
    const char *source,
    const char *detail)
{
    power_manager_record_activity("voice_recording_stop_cleanup");
    s_state = VOICE_RECORDING_STATE_IDLE;
    s_active_session_source = NULL;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    voice_recording_control_log_toggle_ignored(
        source,
        detail != NULL ? detail : "host_cleanup_stop_completed",
        ESP_OK,
        false);
    voice_recording_control_log_device_status("ready", "recording_session_cleanup");
}

static void voice_recording_control_stop(const char *source)
{
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_STOP, &snapshot);

    if (decision.activity != NULL) {
        power_manager_record_activity(decision.activity);
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_CLEAR_PENDING_START) {
        voice_recording_control_cancel_pending_start(decision.detail);
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 2, voice_recording_source_code(source), s_session_count, 0);
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_STOP_CLEANUP) {
        voice_recording_control_complete_transfer_cleanup(source, decision.detail);
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_IGNORE) {
        voice_recording_control_log_toggle_ignored(
            source,
            decision.detail,
            decision.result,
            false);
        voice_recording_control_log_device_status("ready", decision.detail);
        return;
    }

    (void)voice_recording_control_exit_recording(source);
}

static void voice_recording_control_toggle(const char *source)
{
    bool user_start_intent = voice_recording_control_source_is_user_start_intent(source);
    bool host_control = voice_recording_control_source_is_host_control(source);
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_TOGGLE, &snapshot);

    if (decision.effect == VOICE_RECORDING_EFFECT_IGNORE &&
        decision.detail != NULL &&
        strcmp(decision.detail, "toggle_ignored_cancel_pending") == 0) {
        ESP_LOGW(TAG, "recording toggle ignored source=%s: cancel pending", source);
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        return;
    }

    if (s_pending_start) {
        if (decision.activity != NULL) {
            power_manager_record_activity(decision.activity);
        }
        if (host_control) {
            if (decision.effect == VOICE_RECORDING_EFFECT_CLEAR_PENDING_START) {
                voice_recording_control_cancel_pending_start(decision.detail);
                return;
            }
            voice_recording_control_log_toggle_ignored(
                source,
                decision.detail,
                decision.result,
                decision.warn);
            return;
        }

        ESP_LOGI(
            TAG,
            "recording toggle ignored source=%s: pending start source=%s",
            source,
            s_pending_start_source != NULL ? s_pending_start_source : "unknown");
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        voice_recording_control_log_device_status(
            "ready",
            s_pending_start_detail != NULL ? s_pending_start_detail : "recording_waiting_for_ble_audio");
        return;
    }

    if (s_state == VOICE_RECORDING_STATE_IDLE) {
        if (decision.effect == VOICE_RECORDING_EFFECT_IGNORE &&
            decision.detail != NULL &&
            strcmp(decision.detail, "host_cleanup_toggle_ignored_after_abort") == 0) {
            if (decision.activity != NULL) {
                power_manager_record_activity(decision.activity);
            }
            voice_recording_control_log_toggle_ignored(
                source,
                decision.detail,
                decision.result,
                decision.warn);
            voice_recording_control_log_device_status("ready", decision.detail);
            return;
        }

        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        esp_err_t ret = voice_recording_control_enter_recording(source, true, true, 0u);
        if (ret == ESP_ERR_INVALID_STATE && !audio_capture_session_is_active()) {
            voice_recording_control_snapshot_t rejected_snapshot =
                voice_recording_control_make_snapshot(source);
            voice_recording_control_decision_t rejected_decision =
                voice_recording_control_decide_transition(
                    VOICE_RECORDING_EVENT_START_REJECTED_TRANSPORT_NOT_READY,
                    &rejected_snapshot);
            if (user_start_intent) {
                voice_recording_control_schedule_pending_start(
                    source,
                    "audio_transport_not_ready",
                    "recording_waiting_for_ble_audio");
            } else {
                voice_recording_control_log_toggle_ignored(
                    source,
                    rejected_decision.detail,
                    rejected_decision.result,
                    rejected_decision.warn);
                voice_recording_control_log_device_status("ready", rejected_decision.detail);
            }
        }
    } else if (s_state == VOICE_RECORDING_STATE_RECORDING) {
        /*
         * Hidden automatic (voice-activation) candidate is already RECORDING but not
         * visible. A dictation key / host VREC:TOGGLE must take over that stream —
         * not stop it. Stopping here made the first EC11 press a no-op (owner saw a
         * brief Recording capsule, then nothing) while the second press started clean.
         */
        if (s_active_session_automatic && !s_active_session_visible) {
            ESP_LOGI(
                TAG,
                "recording toggle promotes hidden automatic candidate source=%s",
                source);
            voice_recording_control_log_flow(
                VOICE_RECORDING_FLOW_TOGGLE_START,
                "toggle_promotes_hidden_automatic",
                source,
                ESP_OK,
                false);
            if (decision.activity != NULL) {
                power_manager_record_activity(decision.activity);
            }
            (void)voice_recording_control_activate_automatic_session(source);
            return;
        }
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        voice_recording_control_exit_recording(source);
    } else if (s_state == VOICE_RECORDING_STATE_TRANSFERRING) {
        if (user_start_intent) {
            if (decision.activity != NULL) {
                power_manager_record_activity(decision.activity);
            }
            ESP_LOGI(TAG, "recording toggle queued source=%s: previous session transferring", source);
            voice_recording_control_log_flow(
                decision.flow_stage,
                decision.detail,
                source,
                decision.result,
                decision.warn);
            voice_recording_control_schedule_pending_start(
                source,
                "audio_session_transferring",
                "recording_waiting_for_previous_session");
            return;
        }
        voice_recording_control_stop(source);
    } else {
        voice_recording_control_log_toggle_ignored(
            source,
            decision.detail != NULL ? decision.detail :
                (host_control ? "host_cleanup_toggle_ignored_recovery" : "toggle_ignored_recovery"),
            decision.result,
            decision.warn);
    }
}

static void voice_recording_control_cancel(const char *source)
{
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_CANCEL, &snapshot);

    if (decision.activity != NULL) {
        power_manager_record_activity(decision.activity);
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_CLEAR_PENDING_START) {
        voice_recording_control_cancel_pending_start(decision.detail);
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 3, voice_recording_source_code(source), s_session_count, 0);
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_STOP_CLEANUP) {
        voice_recording_control_complete_transfer_cleanup(source, decision.detail);
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_IGNORE) {
        voice_recording_control_log_toggle_ignored(
            source,
            decision.detail,
            decision.result,
            decision.warn);
        voice_recording_control_log_device_status("ready", decision.detail);
        return;
    }

    esp_err_t ret = audio_capture_session_cancel();
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_active_session_source = NULL;
            s_active_session_automatic = false;
            s_active_session_visible = false;
            voice_recording_control_clear_power_blockers();
        }
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "recording_cancel_rejected");
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
        s_active_session_automatic = false;
        s_active_session_visible = false;
        voice_recording_control_clear_power_blockers();
        (void)voice_key_input_set_recording_output(false);
        status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
        status_led_set_processing(false, "recording_canceled");
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

static void voice_recording_control_recovery(
    const char *source,
    bool type_controlled,
    bool suppress_swift_pair_prompt,
    bool manual_pairing_visual)
{
    /*
     * Windows manual deletion is only a user-pairing handoff cue. Do not
     * start a Type-controlled recovery window here: that path deliberately
     * keeps the old identity, which makes the later Swift Pair click target
     * Windows' stale device cache. The physical EC11 double-click owns the
     * actual bond reset and fresh-identity advertisement.
     */
    if (manual_pairing_visual) {
        voice_recording_control_cancel_pending_start("manual_pairing_wait");
        (void)voice_key_input_set_recording_output(false);
        status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
        status_led_set_processing(false, "manual_pairing_wait");
        /*
         * Owner direction 2026-07-21: no extended low-brightness wait cue here.
         * Low-brightness blue is reserved for an established BLE link; the
         * seek-pairing double-blink belongs to the ordinary recovery window
         * (the physical EC11 double-click), so the manual Windows delete path
         * falls back to the pre-existing LED behavior without a 120 s hold.
         */
        voice_recording_control_log_flow(
            VOICE_RECORDING_FLOW_RECOVERY,
            "manual_pairing_wait",
            source,
            ESP_OK,
            false);
        voice_recording_control_log_device_status("pairing", "manual_windows_unpair_wait");
        ESP_LOGI(
            TAG,
            "manual Windows pairing cue armed source=%s; waiting for physical EC11 recovery reset",
            source != NULL ? source : "unknown");
        return;
    }

    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_RECOVERY, &snapshot);
    const bool ec11_fast_idle_recovery =
        source != NULL &&
        strncmp(source, "ec11_", strlen("ec11_")) == 0 &&
        !audio_capture_session_is_active() &&
        !s_pending_start;
    esp_err_t fast_recovery_ret = ESP_OK;

    /*
     * Owner contract (1.0.2 behavior): EC11 double-click always re-pairs.
     * When Type is live, best-effort recovery notice + ACK help Type accept
     * PairAsync cleanly — but a missed notice/ACK must NEVER block bond reset
     * or light a hard red error (zombie Type / post-flash half-link was doing that).
     */
    if (ec11_fast_idle_recovery) {
        if (ble_audio_stream_is_type_link_ready() &&
            ble_audio_stream_was_type_host_recently_seen()) {
            esp_err_t notice_ret = ble_audio_stream_send_type_recovery_notice();
            if (notice_ret != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "EC11 recovery notice could not reach Type before pairing reset: %s; continuing re-pair",
                    esp_err_to_name(notice_ret));
            } else {
                esp_err_t ack_ret = ble_audio_stream_wait_for_type_recovery_ack(
                    BLE_AUDIO_STREAM_TYPE_RECOVERY_ACK_TIMEOUT_MS);
                if (ack_ret != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "EC11 recovery Type ACK timed out after %u ms; continuing re-pair (never block double-click)",
                        BLE_AUDIO_STREAM_TYPE_RECOVERY_ACK_TIMEOUT_MS);
                }
            }
        }
        fast_recovery_ret = ble_hid_gap_forget_bonds_and_repair_ec11_fast();
        /* If the connected/async-fast path fails, still force a full native reset
         * so the user is never stuck with a hard ERROR after double-click. */
        if (fast_recovery_ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "EC11 fast recovery failed (%s); falling back to full forget-and-repair",
                esp_err_to_name(fast_recovery_ret));
            fast_recovery_ret = ble_hid_gap_forget_bonds_and_repair();
        }
    }

    if (decision.activity != NULL) {
        power_manager_record_activity(decision.activity);
    }
    voice_recording_control_log_flow(
        decision.flow_stage,
        decision.detail,
        source,
        decision.result,
        decision.warn);
    voice_recording_control_cancel_pending_start("recovery_cleared_pending_start");
    (void)voice_key_input_set_recording_output(false);
    power_manager_set_blocker(
        POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
        true);
    voice_recording_state_t previous_state = s_state;
    s_state = VOICE_RECORDING_STATE_RECOVERY;
    status_led_notify_ble_repairing("voice_recovery_requested");
    ESP_LOGW(
        TAG,
        "recovery requested source=%s type_controlled=%u suppress_swift_pair=%u: preparing BLE pairing reset",
        source,
        type_controlled ? 1u : 0u,
        suppress_swift_pair_prompt ? 1u : 0u);
    voice_recording_control_log_device_status("recovery", "forget_pairing_and_clear_session");

    if (!ec11_fast_idle_recovery &&
        source != NULL && strncmp(source, "ec11_", strlen("ec11_")) == 0 &&
        ble_audio_stream_is_type_link_ready() &&
        ble_audio_stream_was_type_host_recently_seen()) {
        esp_err_t notice_ret = ble_audio_stream_send_type_recovery_notice();
        if (notice_ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "EC11 recovery notice could not reach Type before pairing reset: %s",
                esp_err_to_name(notice_ret));
        }
    }

    if (audio_capture_session_is_active()) {
        esp_err_t cancel_ret = audio_capture_session_cancel();
        if (cancel_ret == ESP_OK) {
            ESP_LOGW(TAG, "recovery canceled active recording session source=%s", source);
        } else {
            ESP_LOGW(TAG, "recovery session cancel failed source=%s: %s", source, esp_err_to_name(cancel_ret));
            voice_recording_control_log_device_error("error", "recovery_cancel_session_failed", cancel_ret);
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "recovery_cancel_session_failed");
        }
    }

    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = NULL;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    esp_err_t ret = ec11_fast_idle_recovery
        ? fast_recovery_ret
        : (suppress_swift_pair_prompt
            ? ble_hid_gap_forget_bonds_and_repair_type_controlled_silent()
            : (type_controlled
                ? ble_hid_gap_forget_bonds_and_repair_type_controlled()
                : ble_hid_gap_forget_bonds_and_repair()));
    if (ret != ESP_OK) {
        voice_recording_control_log_device_error("error", "recovery_pairing_reset_failed", ret);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "recovery_pairing_reset_failed");
        s_state = previous_state == VOICE_RECORDING_STATE_RECORDING ? VOICE_RECORDING_STATE_RECORDING : VOICE_RECORDING_STATE_IDLE;
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
            false);
        return;
    }

    s_state = VOICE_RECORDING_STATE_IDLE;
    voice_recording_control_clear_power_blockers();
    bool pairing_window_open = ble_hid_gap_is_recovery_pairing_window_open();
    if (pairing_window_open) {
        ESP_LOGI(
            TAG,
            "recovery reset accepted; waiting for a fresh Windows/Type bond while BLE recovery pairing window is open");
    } else {
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_PAIRING | POWER_MANAGER_BLOCKER_RECONNECT,
            false);
    }
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    status_led_set_processing(false, "recovery_complete");
    status_led_set_ble_state(STATUS_LED_BLE_PAIRING, false);
    if (pairing_window_open) {
        voice_recording_control_log_device_status("pairing", "recovery_pairing_window_open");
    } else {
        voice_recording_control_log_device_status("ready", "recovery_complete_no_pairing_window");
    }
}

static void voice_recording_control_handle_fast_idle_ec11_start(uint32_t press_to_control_ms)
{
    voice_recording_state_t state_before = s_state;
    bool pending_start_before = s_pending_start;

    voice_recording_control_toggle("ec11.fast_idle");

    bool suppress_fallback_hid =
        s_state != state_before ||
        s_pending_start != pending_start_before ||
        s_state == VOICE_RECORDING_STATE_RECORDING;
    voice_key_input_complete_fast_idle_recording_event(suppress_fallback_hid);
    ESP_LOGI(
        TAG,
        "EC11 fast Idle recording dispatch: press_to_control_ms=%" PRIu32
        " target_ms=50 fallback_hid_suppressed=%u state_before=%s state_after=%s pending=%u",
        press_to_control_ms,
        suppress_fallback_hid ? 1u : 0u,
        voice_recording_state_name(state_before),
        voice_recording_state_name(s_state),
        s_pending_start ? 1u : 0u);

    /* A pending press must not inherit the previous recording session's ID. */
    uint32_t recording_session_id =
        s_state == VOICE_RECORDING_STATE_RECORDING && !s_pending_start ? s_session_count : 0U;
    diag_log(
        DIAG_SRC_VOICE_REC,
        DIAG_VREC_TIMING,
        DIAG_SEV_INFO,
        press_to_control_ms,
        voice_recording_source_code("ec11.fast_idle"),
        recording_session_id,
        (uint32_t)s_state);
}

static void voice_recording_control_handle_fast_active_ec11_stop(uint32_t press_to_control_ms)
{
    voice_recording_state_t state_before = s_state;

    voice_recording_control_toggle("ec11.fast_active_stop");

    bool suppress_fallback_hid =
        state_before == VOICE_RECORDING_STATE_RECORDING &&
        s_state == VOICE_RECORDING_STATE_TRANSFERRING;
    voice_key_input_complete_fast_active_recording_stop_event(suppress_fallback_hid);
    ESP_LOGI(
        TAG,
        "EC11 fast active recording stop dispatch: press_to_control_ms=%" PRIu32
        " target_ms=50 fallback_hid_suppressed=%u state_before=%s state_after=%s",
        press_to_control_ms,
        suppress_fallback_hid ? 1u : 0u,
        voice_recording_state_name(state_before),
        voice_recording_state_name(s_state));
    diag_log(
        DIAG_SRC_VOICE_REC,
        DIAG_VREC_TIMING,
        DIAG_SEV_INFO,
        press_to_control_ms,
        voice_recording_source_code("ec11.fast_active_stop"),
        s_session_count,
        50U);
}

static void voice_recording_control_host_processing_start(const char *source)
{
    voice_recording_control_note_ble_type_processing_activity(source, "host_processing_start");
    status_led_set_processing(true, "host_processing_start");
    ESP_LOGI(TAG, "host processing start source=%s", source);
    voice_recording_control_log_device_status(voice_recording_state_name(s_state), "host_processing_start");
}

static void voice_recording_control_host_processing_stop(const char *source)
{
    voice_recording_control_note_ble_type_processing_activity(source, "host_processing_stop");
    status_led_set_processing(false, "host_processing_stop");
    ESP_LOGI(TAG, "host processing stop source=%s", source);
    voice_recording_control_log_device_status(voice_recording_state_name(s_state), "host_processing_stop");
}

static void voice_recording_control_host_processing_done(const char *source)
{
    voice_recording_control_note_ble_type_processing_activity(source, "host_processing_done");
    /* notify_success clears AI + shows one OK peak. Do not set_processing(false)
     * first — that schedules an idle black frame and makes OK look like two greens. */
    status_led_notify_success("host_processing_done");
    ESP_LOGI(TAG, "host processing done source=%s", source);
    voice_recording_control_log_device_status(voice_recording_state_name(s_state), "host_processing_done");
}

static void voice_recording_control_host_processing_warning(const char *source)
{
    voice_recording_control_note_ble_type_processing_activity(source, "host_processing_warning");
    /* Same handoff as DONE: warning owns the result window without a clear flash. */
    status_led_notify_warning("host_processing_warning");
    ESP_LOGI(TAG, "host processing warning source=%s", source);
    voice_recording_control_log_device_status(voice_recording_state_name(s_state), "host_processing_warning");
}

static esp_err_t voice_recording_control_activate_automatic_session(
    const char *source)
{
    if (s_state != VOICE_RECORDING_STATE_RECORDING ||
        !s_active_session_automatic) {
        ESP_LOGW(
            TAG,
            "automatic recording activation rejected source=%s state=%s automatic=%u",
            source,
            voice_recording_state_name(s_state),
            s_active_session_automatic ? 1u : 0u);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_active_session_visible) {
        return ESP_OK;
    }

    /*
     * The hidden VAD candidate can spend several seconds in the recording
     * state while Type verifies the wake phrase and speaker. Endpointing for
     * the visible dictation must start at acceptance, not at candidate start.
     */
    denzic_voice_activation_v1_reset(
        &s_voice_activation_machine);
    s_active_session_visible = true;
    (void)voice_key_input_set_recording_output(true);
    status_led_set_recording(true, STATUS_LED_REC_SOURCE_DEVICE_MIC);
    ESP_LOGI(
        TAG,
        "automatic recording activated with fresh endpoint window source=%s",
        source);
    voice_recording_control_log_device_status(
        "recording",
        "automatic_candidate_accepted");
    return ESP_OK;
}

esp_err_t voice_recording_control_dispatch_control_command(const char *command, const char *source)
{
    if (command == NULL || source == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!voice_recording_control_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    const char *action = command;
    if (strncmp(
            command,
            VOICE_RECORDING_CONTROL_VREC_PREFIX,
            strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX)) == 0) {
        action = command + strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX);
    }

    if (strcmp(action, "TOGGLE") == 0) {
        voice_recording_control_toggle(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "CANCEL") == 0) {
        voice_recording_control_cancel(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "ACTIVATE") == 0) {
        esp_err_t ret =
            voice_recording_control_activate_automatic_session(source);
        voice_recording_control_unlock();
        return ret;
    }
    if (strcmp(action, "PROCESSING:START") == 0 || strcmp(action, "PROCESSING_START") == 0) {
        voice_recording_control_host_processing_start(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:STOP") == 0 || strcmp(action, "PROCESSING_STOP") == 0) {
        voice_recording_control_host_processing_stop(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:DONE") == 0 || strcmp(action, "PROCESSING_DONE") == 0) {
        voice_recording_control_host_processing_done(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:WARN") == 0 ||
        strcmp(action, "PROCESSING_WARNING") == 0 ||
        strcmp(action, "PROCESSING:WARNING") == 0 ||
        strcmp(action, "PROCESSING_WARN") == 0 ||
        strcmp(action, "PROCESSING:FAIL") == 0 ||
        strcmp(action, "PROCESSING_FAIL") == 0 ||
        strcmp(action, "PROCESSING:FAILED") == 0 ||
        strcmp(action, "PROCESSING_FAILED") == 0 ||
        strcmp(action, "PROCESSING:ERROR") == 0 ||
        strcmp(action, "PROCESSING_ERROR") == 0) {
        voice_recording_control_host_processing_warning(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "STOP") == 0 || strcmp(action, "CLEANUP") == 0) {
        voice_recording_control_stop(source);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "RECOVERY:TYPE:MANUAL") == 0) {
        voice_recording_control_recovery(source, true, false, true);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "RECOVERY:TYPE:SILENT") == 0 ||
        strcmp(action, "RECOVERY_TYPE_SILENT") == 0 ||
        strcmp(action, "RECOVERY:TYPE_NO_PROMPT") == 0 ||
        strcmp(action, "RECOVERY_TYPE_NO_PROMPT") == 0) {
        voice_recording_control_recovery(source, true, true, false);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "RECOVERY:TYPE") == 0 || strcmp(action, "RECOVERY_TYPE") == 0) {
        voice_recording_control_recovery(source, true, false, false);
        voice_recording_control_unlock();
        return ESP_OK;
    }
    if (strcmp(action, "RECOVERY") == 0 || strcmp(action, "RESET") == 0 || strcmp(action, "FORGET") == 0) {
        voice_recording_control_recovery(source, false, false, false);
        voice_recording_control_unlock();
        return ESP_OK;
    }

    ESP_LOGW(TAG, "drop control command source=%s command=%s", source, command);
    voice_recording_control_log_device_error("error", "unknown_control_command", ESP_ERR_INVALID_ARG);
    voice_recording_control_unlock();
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t voice_recording_control_ble_control_write(
    const uint8_t *data,
    size_t len,
    const char *source)
{
    if (data == NULL || len == 0 || len >= VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    char command[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
    memcpy(command, data, len);
    command[len] = '\0';
    command[strcspn(command, "\r\n")] = '\0';

    if (ble_audio_stream_consume_type_control_command(command, source)) {
        return ESP_OK;
    }

    power_manager_record_activity("voice_recording_ble_control");
    return voice_recording_control_dispatch_control_command(command, source);
}

static void voice_recording_control_poll_pending_start(void)
{
    if (!s_pending_start) {
        return;
    }

    if (s_state == VOICE_RECORDING_STATE_TRANSFERRING) {
        return;
    }

    if (s_state != VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_reset_pending_start();
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (voice_recording_control_tick_reached(now, s_pending_start_deadline_tick)) {
        voice_recording_control_snapshot_t timeout_snapshot =
            voice_recording_control_make_snapshot(s_pending_start_source);
        voice_recording_control_decision_t timeout_decision =
            voice_recording_control_decide_transition(
                VOICE_RECORDING_EVENT_PENDING_TIMEOUT,
                &timeout_snapshot);
        voice_recording_control_timeout_pending_start(timeout_decision.result);
        return;
    }

    if (!voice_recording_control_tick_reached(now, s_pending_start_next_retry_tick)) {
        return;
    }
    s_pending_start_next_retry_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS);

    if (!ble_audio_stream_is_ready()) {
        return;
    }

    voice_recording_control_snapshot_t pending_snapshot =
        voice_recording_control_make_snapshot(s_pending_start_source);
    voice_recording_control_decision_t pending_decision =
        voice_recording_control_decide_transition(
            VOICE_RECORDING_EVENT_PENDING_READY,
            &pending_snapshot);
    const char *source = s_pending_start_source != NULL ? s_pending_start_source : "pending";
    ESP_LOGI(TAG, "recording pending start transport ready source=%s", source);
    voice_recording_control_log_flow(
        pending_decision.flow_stage,
        pending_decision.detail,
        source,
        pending_decision.result,
        pending_decision.warn);
    esp_err_t ret = voice_recording_control_enter_recording(source, false, false, 0u);
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

static void voice_recording_control_handle_session_inactive(void)
{
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(
        s_cancel_pending ?
            (s_cancel_source != NULL ? s_cancel_source : "unknown") :
            (s_active_session_source != NULL ? s_active_session_source : "session"));
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(
            VOICE_RECORDING_EVENT_SESSION_INACTIVE,
            &snapshot);

    if (decision.effect == VOICE_RECORDING_EFFECT_IGNORE) {
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_FINISH_CANCEL) {
        const char *source = s_cancel_source != NULL ? s_cancel_source : "unknown";
        s_cancel_pending = false;
        s_cancel_source = NULL;
        s_state = decision.next_state;
        s_active_session_source = NULL;
        s_active_session_automatic = false;
        s_active_session_visible = false;
        voice_recording_control_clear_power_blockers();
        (void)voice_key_input_set_recording_output(false);
        status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
        status_led_set_processing(false, "recording_canceled");
        ESP_LOGI(TAG, "recording cancel source=%s", source);
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        voice_recording_control_log_device_status("ready", "recording_canceled");
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 3, voice_recording_source_code(source), s_session_count, 0);
        return;
    }

    const char *source = s_active_session_source != NULL ? s_active_session_source : "session";
    s_state = decision.next_state;
    s_active_session_source = NULL;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);

    if (decision.effect == VOICE_RECORDING_EFFECT_FINISH_TRANSFER) {
        ESP_LOGI(TAG, "recording session finished");
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        voice_recording_control_log_device_status("ready", "recording_session_finished");
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_FINISH_ABORT) {
        ESP_LOGW(TAG, "recording session ended without stop request");
        voice_recording_control_log_flow(
            decision.flow_stage,
            decision.detail,
            source,
            decision.result,
            decision.warn);
        voice_recording_control_log_device_error("ready", "recording_session_aborted", decision.result);
        if (voice_recording_control_source_is_host_control(source)) {
            voice_recording_control_arm_host_cleanup_toggle_guard();
        }
    }
}

static void voice_recording_control_on_voice_activity(
    bool speech_detected,
    uint32_t elapsed_ms)
{
    if (s_vad_queue == NULL) {
        return;
    }
    voice_recording_control_vad_event_t event = {
        .speech_detected = speech_detected,
        .elapsed_ms = elapsed_ms,
    };
    (void)xQueueSend(s_vad_queue, &event, 0);
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

static void voice_recording_control_refresh_voice_monitoring(void)
{
    device_settings_snapshot_t settings = {0};
    power_manager_snapshot_t power = {0};
    device_settings_get_snapshot(&settings);
    power_manager_get_snapshot(&power);
    s_voice_auto_start_enabled = settings.voice_auto_start_enabled;
    s_voice_auto_stop_enabled = settings.voice_auto_stop_enabled;
    if (!s_voice_auto_start_enabled &&
        s_active_session_automatic &&
        s_state == VOICE_RECORDING_STATE_RECORDING &&
        !s_cancel_pending) {
        denzic_voice_activation_v1_reset(
            &s_voice_activation_machine);
        voice_recording_control_cancel(
            "voice_activation.auto_start_disabled");
    }
    bool idle_start_monitoring =
        s_state == VOICE_RECORDING_STATE_IDLE &&
        ble_audio_stream_is_ready() &&
        s_voice_auto_start_enabled;
    bool active_stop_monitoring =
        s_state == VOICE_RECORDING_STATE_RECORDING &&
        !s_cancel_pending &&
        (s_active_session_automatic || s_voice_auto_stop_enabled);
    bool monitoring =
        power.state == POWER_MANAGER_STATE_ACTIVE &&
        (idle_start_monitoring || active_stop_monitoring);
    if (monitoring == s_voice_monitoring) {
        return;
    }
    esp_err_t ret =
        audio_capture_set_voice_activation_monitoring(monitoring);
    if (ret == ESP_OK) {
        s_voice_monitoring = monitoring;
    } else {
        monitoring = false;
        s_voice_monitoring = false;
    }
    if (!monitoring) {
        denzic_voice_activation_v1_reset(
            &s_voice_activation_machine);
        if (s_vad_queue != NULL) {
            xQueueReset(s_vad_queue);
        }
    }
    ESP_LOGI(
        TAG,
        "voice activation monitoring=%u auto_start=%u auto_stop=%u power=%s ble_ready=%u ret=%s",
        monitoring ? 1u : 0u,
        s_voice_auto_start_enabled ? 1u : 0u,
        s_voice_auto_stop_enabled ? 1u : 0u,
        power_manager_state_name(power.state),
        ble_audio_stream_is_ready() ? 1u : 0u,
        esp_err_to_name(ret));
}

static void voice_recording_control_process_voice_activity(void)
{
    voice_recording_control_vad_event_t event = {0};
    while (s_vad_queue != NULL &&
           xQueueReceive(s_vad_queue, &event, 0) == pdTRUE) {
        denzic_voice_activation_v1_input_t input = {
            .elapsed_ms = event.elapsed_ms,
            .enabled = s_voice_monitoring,
            .auto_start_enabled = s_voice_auto_start_enabled,
            .auto_stop_enabled = s_voice_auto_stop_enabled,
            .recording_active =
                s_state == VOICE_RECORDING_STATE_RECORDING,
            .speech_detected = event.speech_detected,
            .start_blocked =
                s_state != VOICE_RECORDING_STATE_IDLE ||
                s_pending_start ||
                s_cancel_pending,
        };
        denzic_voice_activation_v1_decision_t decision =
            denzic_voice_activation_v1_step(
                &s_voice_activation_machine,
                &s_voice_activation_config,
                &input);
        if (decision.action ==
                DENZIC_VOICE_ACTIVATION_V1_ACTION_START &&
            s_state == VOICE_RECORDING_STATE_IDLE &&
            !s_pending_start) {
            (void)voice_recording_control_enter_recording(
                "voice_activation.auto_start",
                true,
                false,
                decision.pre_roll_ms);
        } else if (
            decision.action ==
                DENZIC_VOICE_ACTIVATION_V1_ACTION_STOP &&
            s_state == VOICE_RECORDING_STATE_RECORDING) {
            (void)voice_recording_control_exit_recording_with_origin(
                decision.stop_reason ==
                        DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION
                    ? "voice_activation.auto_stop_max_duration"
                    : "voice_activation.auto_stop_silence",
                AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION);
        }
    }
}

static void voice_recording_control_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("voice_recording_control_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (voice_recording_control_lock()) {
            voice_recording_control_refresh_voice_monitoring();
            voice_recording_control_process_voice_activity();
            uint32_t press_to_control_ms = 0;
            if (voice_key_input_take_fast_idle_recording_event(&press_to_control_ms)) {
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
                voice_recording_control_handle_fast_idle_ec11_start(press_to_control_ms);
            }

            if (voice_key_input_take_fast_idle_recording_cancel_event()) {
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
                voice_recording_control_cancel("ec11.fast_idle_long_press");
            }

            if (voice_key_input_take_fast_active_recording_stop_event(&press_to_control_ms)) {
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
                voice_recording_control_handle_fast_active_ec11_stop(press_to_control_ms);
            }

            if (voice_key_input_take_toggle_event()) {
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
                voice_recording_control_toggle(voice_key_input_get_active_source());
            }

            uint64_t recovery_accepted_at_us = 0;
            bool recovery_generated = false;
            if (voice_key_input_take_recovery_event(
                    &recovery_accepted_at_us,
                    &recovery_generated)) {
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
                const char *source = voice_key_input_get_active_source();
                ble_hid_gap_note_ec11_recovery_accepted(
                    recovery_accepted_at_us,
                    recovery_generated);
                voice_recording_control_recovery(source != NULL ? source : "voice_key_hold", false, false, false);
            }

            if ((s_state == VOICE_RECORDING_STATE_RECORDING || s_state == VOICE_RECORDING_STATE_TRANSFERRING) &&
                !audio_capture_session_is_active()) {
                voice_recording_control_handle_session_inactive();
            }

            voice_recording_control_poll_pending_start();
            voice_recording_control_unlock();
        }

        (void)watchdog_platform_task_notify_take(
            pdTRUE,
            VOICE_RECORDING_CONTROL_SESSION_CHECK_MS);
    }
}

uint32_t voice_recording_control_get_session_count(void)
{
    uint32_t session_count = 0;
    if (voice_recording_control_lock()) {
        session_count = s_session_count;
        voice_recording_control_unlock();
    }
    return session_count;
}

static esp_err_t voice_recording_control_start_internal(bool enable_audio_capture)
{
    if (s_started) {
        return ESP_OK;
    }
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_vad_queue == NULL) {
        s_vad_queue = xQueueCreate(
            VOICE_RECORDING_CONTROL_VAD_QUEUE_LENGTH,
            sizeof(voice_recording_control_vad_event_t));
        if (s_vad_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_voice_activation_config =
        denzic_voice_activation_v1_default_config();
    s_voice_activation_config.silence_stop_ms = 1800u;
    s_voice_activation_config.tail_ms = 350u;
    denzic_voice_activation_v1_reset(
        &s_voice_activation_machine);
    if (enable_audio_capture) {
        audio_capture_set_voice_activity_handler(
            voice_recording_control_on_voice_activity);
    }

    esp_err_t key_ret = voice_key_input_start();
    if (key_ret != ESP_OK) {
        ESP_LOGW(TAG, "voice key input start failed; USB recovery remains available: %s", esp_err_to_name(key_ret));
        voice_recording_control_log_device_error("error", "voice_key_input_start_failed", key_ret);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "voice_key_input_start_failed");
    }

    esp_err_t audio_ret = ESP_OK;
    if (enable_audio_capture) {
        audio_ret = audio_capture_start();
        if (audio_ret != ESP_OK) {
            ESP_LOGW(TAG, "audio capture start failed; keeping recovery/status path alive: %s", esp_err_to_name(audio_ret));
            voice_recording_control_log_device_error("error", "audio_capture_start_failed", audio_ret);
            status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "audio_capture_start_failed");
        }
    } else {
        ESP_LOGW(
            TAG,
            "recovery-only start: audio capture left disabled (EC11 double-click re-pair still active)");
        status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
        audio_ret = ESP_ERR_NOT_SUPPORTED;
    }

    ble_audio_stream_set_control_write_handler(voice_recording_control_ble_control_write);

    static StaticTask_t s_voice_recording_control_task_control;
    static StackType_t *s_voice_recording_control_task_stack;
    BaseType_t task_ok = watchdog_platform_start_task_on_spiram(
        voice_recording_control_task,
        "voice_recording_control_task",
        4096,
        5,
        &s_task_handle,
        &s_voice_recording_control_task_control,
        &s_voice_recording_control_task_stack);
    if (task_ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    voice_key_input_set_recording_control_task(s_task_handle);

    s_started = true;
    ESP_LOGI(
        TAG,
        "voice recording control ready: source=%s audio=%u toggle start/stop fsm_artifact_cases=%u",
        voice_key_input_get_active_source(),
        enable_audio_capture ? 1u : 0u,
        (unsigned)VOICE_RECORDING_CONTROL_ARRAY_SIZE(VOICE_RECORDING_CONTROL_FSM_ARTIFACT));
    if (audio_ret == ESP_OK && key_ret == ESP_OK) {
        voice_recording_control_log_device_status("ready", "voice_recording_control_started");
        status_led_show_status_window("voice_recording_control_started");
        return ESP_OK;
    }

    if (key_ret != ESP_OK) {
        voice_recording_control_log_device_error("error", "voice_recording_control_input_failed", key_ret);
        return key_ret;
    }

    voice_recording_control_log_device_error(
        "degraded",
        enable_audio_capture ? "voice_recording_control_audio_degraded" : "voice_recording_control_recovery_only",
        audio_ret);
    return ESP_OK;
}

esp_err_t voice_recording_control_start(void)
{
    /*
     * Defer mic/AFE until voice_recording_control_enable_audio() after BLE host
     * start. Control + EC11 recovery still come up immediately.
     */
    return voice_recording_control_start_internal(false);
}

esp_err_t voice_recording_control_start_recovery_only(void)
{
    return voice_recording_control_start_internal(false);
}

esp_err_t voice_recording_control_enable_audio(void)
{
    if (!s_started) {
        esp_err_t start_ret = voice_recording_control_start_internal(true);
        return start_ret;
    }

    audio_capture_set_voice_activity_handler(
        voice_recording_control_on_voice_activity);
    esp_err_t audio_ret = audio_capture_start();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "deferred audio capture start failed; recovery path stays alive: %s",
            esp_err_to_name(audio_ret));
        voice_recording_control_log_device_error("error", "audio_capture_start_failed", audio_ret);
        status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "audio_capture_start_failed");
        voice_recording_control_log_device_error(
            "degraded",
            "voice_recording_control_audio_degraded",
            audio_ret);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "deferred audio capture started after BLE host");
    voice_recording_control_log_device_status("ready", "voice_recording_control_audio_enabled");
    status_led_show_status_window("voice_recording_control_audio_enabled");
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
        (void)voice_recording_control_dispatch_control_command(s_usb_command_buffer, "usb");
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
