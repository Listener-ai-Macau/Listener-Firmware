#include "voice_recording_control.h"
#include "voice_recording_endpoint_policy.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "diag_log.h"

#include "audio_capture.h"
#include "ble_audio_stream.h"
#include "ble_hid_gap.h"
#include "denzic_voice_activation_v1.h"
#include "device_settings.h"
#include "listener_device.h"
#include "listener_audio_proto.h"
#include "power_manager.h"
#include "status_led.h"
#include "voice_key_input.h"
#include "watchdog_platform.h"
#include "voice_stack_diag.h"

#define VOICE_RECORDING_CONTROL_PREFIX_CHAR '~'
#define VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES 32
#define VOICE_RECORDING_CONTROL_VREC_PREFIX "VREC:"
#define VOICE_RECORDING_CONTROL_DIAG_STATE_PREFIX "DIAG:STATE:"
#define VOICE_RECORDING_CONTROL_DIAG_REQUEST_ID_MAX_DIGITS 10u
#define VOICE_RECORDING_CONTROL_SESSION_CHECK_MS 50
#define VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS 12000
#define VOICE_RECORDING_CONTROL_ENSURE_PENDING_TIMEOUT_MS 3000
#define VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS 250
#define VOICE_RECORDING_CONTROL_ENROLLMENT_START_TIMEOUT_MS 6000
#define VOICE_RECORDING_CONTROL_ENROLLMENT_START_RETRY_MS 100
#define VOICE_RECORDING_CONTROL_ENROLLMENT_STALE_STOP_GUARD_MS 2000
#define VOICE_RECORDING_CONTROL_HOST_CLEANUP_TOGGLE_GUARD_MS 1500
/* The product endpoint is one second after body speech. Keep the firmware
 * transport fallback aligned with Type so provider stalls cannot make the same
 * ending feel slower. Wake-only startup protection remains a separate Type
 * policy and must not be implemented by stretching this dictation timer. */
#define VOICE_RECORDING_CONTROL_DICTATION_SILENCE_STOP_MS 1000
/* A visible session that has not received any body-speech evidence must leave
 * enough time for a natural post-click / post-wake breath. Once Type sends
 * authoritative VREC:SPEECH, the strict one-second body endpoint above takes
 * over immediately. Local firmware VAD must not select that boundary because
 * low-energy continuous speech can contain false one-second VAD gaps. Hidden
 * wake candidates keep their existing short max-session contract. */
#define VOICE_RECORDING_CONTROL_INITIAL_BODY_WAIT_MS 4000
#define VOICE_RECORDING_CONTROL_DICTATION_TAIL_MS 0
/* Hidden VoiceActivation is a wake candidate, not a dictation session.
 * 2 s pre-roll + ~2 s for a complete 开始录音 (four characters ~0.8 s plus
 * trailing audio). 2600 ms left only ~0.6 s after pre-roll and cut the phrase.
 * Type still requires KWS/local-ASR evidence, so the extra window is not a
 * false-wake gate. Visible owner endpoint is unchanged. */
#define VOICE_RECORDING_CONTROL_AUTOMATIC_WAKE_MAX_SESSION_MS 4000u
#define VOICE_RECORDING_CONTROL_HOST_SPEECH_PROTECTION_MS 1000
/* Only a failed max-duration stop arms the recovery latch. A successful hidden
 * window rotation is not rejection of the current VAD episode: transport
 * draining/ready and ordinary speech confirmation own the next start, with the
 * existing pre-roll preserving a phrase crossing the boundary. Waiting for a
 * quiet edge under continuous playback would leave an unexported audio gap.
 * The recovery latch waits for a VAD quiet edge, but VAD events stop
 * entirely when the audio pipeline is starved (empty AFE ring).  Release the
 * latch after this much VAD silence so a mute producer cannot hold the wake
 * rotation dead forever; a real quiet edge still releases it earlier. */
#define VOICE_RECORDING_CONTROL_AUTO_REARM_VAD_TIMEOUT_MS 10000
/* 2026-09-22 noisy-room probe: the quiet edge this latch waits for never
 * arrives while others keep talking, and the latch then deafens hidden wake
 * listening for the whole chatter episode (measured 5.6-8.7 s, unbounded in
 * principle; owner's immediate next wake phrase is swallowed whole). Probe
 * release: hold the latch at most PROBE_FIRST_MS, then allow the next window
 * to start on live VAD; consecutive probe releases back off by +1 s up to
 * PROBE_MAX_MS. A real quiet edge still releases earlier and resets the
 * backoff. PROBE_FIRST_MS equals the VA pre-roll (2000 ms), so a wake phrase
 * spoken right after the previous window rotated is head-rescued by the next
 * probe window's pre-roll. The starved-pipeline backstop above is unchanged
 * (no VAD events at all still waits the full 10 s). */
#define VOICE_RECORDING_CONTROL_AUTO_REARM_PROBE_FIRST_MS 2000u
#define VOICE_RECORDING_CONTROL_AUTO_REARM_PROBE_MAX_MS 5000u
#define VOICE_RECORDING_CONTROL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define VOICE_RECORDING_CONTROL_VAD_QUEUE_LENGTH 24
#define VOICE_RECORDING_CONTROL_COMMAND_QUEUE_LENGTH 24
#define VOICE_RECORDING_CONTROL_SOURCE_BYTES 32
#define VOICE_RECORDING_CONTROL_STORED_SOURCE_BYTES 64
#define VOICE_RECORDING_CONTROL_COMMANDS_PER_TICK 2
#define VOICE_RECORDING_CONTROL_ENSURE_SOURCE "ensure_wake_continuation"
#define VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS 8u

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
    VOICE_RECORDING_ENSURE_STATUS_NONE = 0,
    VOICE_RECORDING_ENSURE_STATUS_PENDING,
    VOICE_RECORDING_ENSURE_STATUS_ACTIVE,
    VOICE_RECORDING_ENSURE_STATUS_DONE,
    VOICE_RECORDING_ENSURE_STATUS_ABORTED,
    VOICE_RECORDING_ENSURE_STATUS_FAILED,
} voice_recording_ensure_status_t;

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

typedef struct {
    char command[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
    char source[VOICE_RECORDING_CONTROL_SOURCE_BYTES];
} voice_recording_control_command_t;

typedef enum {
    VOICE_RECORDING_PENDING_OWNER_NONE = 0,
    VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE,
    VOICE_RECORDING_PENDING_OWNER_EXPLICIT_CONTROL,
    VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION,
} voice_recording_pending_owner_t;

static const char *TAG = "voice_rec_ctrl";

static bool s_started;
static TaskHandle_t s_task_handle;
static bool s_usb_command_active;
static size_t s_usb_command_length;
static char s_usb_command_buffer[VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES];
static voice_recording_state_t s_state = VOICE_RECORDING_STATE_IDLE;
static bool s_cancel_pending;
static const char *s_cancel_source;
static char s_cancel_source_storage[VOICE_RECORDING_CONTROL_STORED_SOURCE_BYTES];
static bool s_pending_start;
static voice_recording_pending_owner_t s_pending_start_owner;
static const char *s_pending_start_source;
static char s_pending_start_source_storage[VOICE_RECORDING_CONTROL_STORED_SOURCE_BYTES];
static const char *s_pending_start_reason;
static const char *s_pending_start_detail;
static TickType_t s_pending_start_deadline_tick;
static TickType_t s_pending_start_next_retry_tick;
static const char *s_active_session_source;
static char s_active_session_source_storage[VOICE_RECORDING_CONTROL_STORED_SOURCE_BYTES];
static bool s_active_session_automatic;
static bool s_active_session_visible;
/* The control actor owns this identity through asynchronous PCM teardown.
 * audio_capture_session_id() becomes zero before the actor observes INACTIVE. */
static uint32_t s_active_session_id;
static bool s_active_session_body_speech_seen;
/* Accepted wake is an endpoint ownership transfer, including when ACTIVATE
 * already made the predecessor visible before ENSURE arrived. It must not
 * require stopping the microphone to acknowledge the host transaction. */
static bool s_active_session_host_endpoint_owned;
static bool s_host_endpoint_ownership_logged;
/* Explicit owner enrollment is host-timed. It must not inherit the normal
 * dictation silence endpoint, otherwise the gap between the three wake-phrase
 * repetitions and the free-speech prompt truncates the biometric sample. */
static bool s_active_session_enrollment;
/* VREC:ENROLL may arrive while ambient VAD is draining a hidden candidate.
 * Queue the owner session across that exact boundary instead of silently
 * accepting the GATT write and misclassifying the next automatic candidate. */
static bool s_enrollment_start_pending;
static TickType_t s_enrollment_start_deadline_tick;
static TickType_t s_enrollment_start_next_retry_tick;
static TickType_t s_enrollment_stale_stop_guard_until_tick;
static TickType_t s_host_speech_protect_until_tick;
static TickType_t s_host_cleanup_toggle_guard_until_tick;
static uint32_t s_session_count;
static QueueHandle_t s_vad_queue;
static uint32_t s_vad_queue_drop_count;
/* Monotonic VAD event counter (any speech state) written from the audio
 * fetch-task callback; the auto re-arm latch timeout below detects "no VAD
 * event at all" by comparing against its latched baseline. */
static volatile uint32_t s_vad_event_total;
static QueueHandle_t s_command_queue;
static bool s_power_state_refresh_pending;
static bool s_voice_monitoring;
static bool s_voice_auto_start_enabled;
static bool s_voice_auto_stop_enabled;
static bool s_voice_auto_rearm_await_quiet_edge;
static uint32_t s_voice_auto_rearm_episode_id;
static uint32_t s_voice_auto_rearm_terminal_session_count;
static bool s_voice_auto_rearm_suppression_logged;
static TickType_t s_voice_auto_rearm_latch_set_tick;
static uint32_t s_voice_auto_rearm_latch_vad_events;
/* Consecutive stop-failure recovery probes since the last quiet edge; backs the
 * probe hold time off 2 s -> 3 s -> ... -> 5 s. Reset by a real quiet edge. */
static uint32_t s_voice_auto_rearm_probe_count;
static denzic_voice_activation_v1_machine_t s_voice_activation_machine;
static denzic_voice_activation_v1_config_t s_voice_activation_config;
/* ENSURE is a host/device transaction, not another toggle edge.  Keep its
 * identity across retries so a duplicate write cannot start or stop a second
 * capture, while a different request cannot steal an in-flight continuation. */
static bool s_ensure_valid;
static uint32_t s_ensure_request_id;
static uint32_t s_ensure_previous_segment_id;
static uint32_t s_ensure_actual_segment_id;
static voice_recording_ensure_status_t s_ensure_status;
static TickType_t s_ensure_deadline_tick;
static bool s_ensure_cancelled_request_valid;
static uint32_t s_ensure_cancelled_request_id;
static bool s_ensure_request_watermark_valid;
static uint32_t s_ensure_request_watermark;

static void voice_recording_control_set_auto_rearm_latch(
    const char *terminal_reason)
{
    if (!s_voice_auto_rearm_await_quiet_edge) {
        s_voice_auto_rearm_episode_id++;
    }
    s_voice_auto_rearm_await_quiet_edge = true;
    s_voice_auto_rearm_terminal_session_count = s_session_count;
    s_voice_auto_rearm_suppression_logged = false;
    s_voice_auto_rearm_latch_set_tick = xTaskGetTickCount();
    s_voice_auto_rearm_latch_vad_events =
        __atomic_load_n(&s_vad_event_total, __ATOMIC_RELAXED);
    ESP_LOGI(
        TAG,
        "voice auto re-arm latch set await_quiet_edge=1 episode=%" PRIu32
        " terminal_session_count=%" PRIu32 " terminal_reason=%s",
        s_voice_auto_rearm_episode_id,
        s_voice_auto_rearm_terminal_session_count,
        terminal_reason != NULL ? terminal_reason : "unknown");
}

static void voice_recording_control_clear_auto_rearm_latch(
    const char *clear_reason)
{
    if (!s_voice_auto_rearm_await_quiet_edge) {
        return;
    }
    ESP_LOGI(
        TAG,
        "voice auto re-arm latch clear await_quiet_edge=0 episode=%" PRIu32
        " terminal_session_count=%" PRIu32 " clear_reason=%s",
        s_voice_auto_rearm_episode_id,
        s_voice_auto_rearm_terminal_session_count,
        clear_reason != NULL ? clear_reason : "unknown");
    s_voice_auto_rearm_await_quiet_edge = false;
    s_voice_auto_rearm_terminal_session_count = 0u;
    s_voice_auto_rearm_suppression_logged = false;
    /* Every latch clear starts a fresh episode for the probe backoff; the
     * probe_due caller re-arms the ladder right after this reset. */
    s_voice_auto_rearm_probe_count = 0u;
}

static bool voice_recording_control_note_auto_rearm_vad_event(
    bool speech_detected)
{
    if (!s_voice_auto_rearm_await_quiet_edge) {
        return false;
    }
    if (!speech_detected) {
        voice_recording_control_clear_auto_rearm_latch("vad_quiet_edge");
        return false;
    }
    if (!s_voice_auto_rearm_suppression_logged &&
        s_state == VOICE_RECORDING_STATE_IDLE &&
        !s_pending_start &&
        !s_cancel_pending) {
        ESP_LOGI(
            TAG,
            "voice auto re-arm candidate suppressed await_quiet_edge=1"
            " episode=%" PRIu32 " terminal_session_count=%" PRIu32,
            s_voice_auto_rearm_episode_id,
            s_voice_auto_rearm_terminal_session_count);
        s_voice_auto_rearm_suppression_logged = true;
    }
    return true;
}

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
static esp_err_t voice_recording_control_adopt_active_wake_session(const char *source);
static void voice_recording_control_try_start_pending_enrollment(const char *context);
static esp_err_t voice_recording_control_ensure_wake_session(
    const char *action,
    const char *source);
static esp_err_t voice_recording_control_abort_wake_session(
    const char *action,
    const char *source);
static bool voice_recording_control_schedule_ensure_successor(
    uint32_t previous_segment_id,
    const char *source);

static const char *voice_recording_control_ensure_status_name(
    voice_recording_ensure_status_t status)
{
    switch (status) {
    case VOICE_RECORDING_ENSURE_STATUS_PENDING:
        return "pending";
    case VOICE_RECORDING_ENSURE_STATUS_ACTIVE:
        return "active";
    case VOICE_RECORDING_ENSURE_STATUS_DONE:
        return "done";
    case VOICE_RECORDING_ENSURE_STATUS_ABORTED:
        return "aborted";
    case VOICE_RECORDING_ENSURE_STATUS_FAILED:
        return "failed";
    case VOICE_RECORDING_ENSURE_STATUS_NONE:
    default:
        return "none";
    }
}

static bool voice_recording_control_parse_hex_u32_fixed(
    const char *digits,
    size_t length,
    uint32_t *value)
{
    if (digits == NULL || value == NULL ||
        length != VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS) {
        return false;
    }

    uint32_t parsed = 0u;
    for (size_t index = 0; index < length; index++) {
        const char digit = digits[index];
        uint32_t nibble;
        if (digit >= '0' && digit <= '9') {
            nibble = (uint32_t)(digit - '0');
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = (uint32_t)(digit - 'A') + 10u;
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = (uint32_t)(digit - 'a') + 10u;
        } else {
            return false;
        }
        parsed = (parsed << 4u) | nibble;
    }
    *value = parsed;
    return parsed != 0u;
}

static bool voice_recording_control_parse_ensure_action(
    const char *action,
    uint32_t *request_id,
    uint32_t *previous_segment_id)
{
    static const char prefix[] = "ENSURE:";
    const size_t prefix_length = sizeof(prefix) - 1u;
    if (action == NULL || request_id == NULL || previous_segment_id == NULL ||
        strncmp(action, prefix, prefix_length) != 0) {
        return false;
    }
    const char *request_digits = action + prefix_length;
    if (request_digits[VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS] != ':') {
        return false;
    }
    const char *previous_digits =
        request_digits + VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS + 1u;
    if (previous_digits[VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS] != '\0') {
        return false;
    }
    return voice_recording_control_parse_hex_u32_fixed(
               request_digits,
               VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS,
               request_id) &&
        voice_recording_control_parse_hex_u32_fixed(
            previous_digits,
            VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS,
            previous_segment_id);
}

static bool voice_recording_control_parse_abort_action(
    const char *action,
    uint32_t *request_id)
{
    static const char prefix[] = "ABORT:";
    const size_t prefix_length = sizeof(prefix) - 1u;
    if (action == NULL || request_id == NULL ||
        strncmp(action, prefix, prefix_length) != 0) {
        return false;
    }
    const char *digits = action + prefix_length;
    if (digits[VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS] != '\0') {
        return false;
    }
    return voice_recording_control_parse_hex_u32_fixed(
        digits,
        VOICE_RECORDING_CONTROL_ENSURE_HEX_DIGITS,
        request_id);
}

static uint16_t voice_recording_control_ensure_start_origin(uint32_t request_id)
{
    return (uint16_t)(
        LISTENER_AUDIO_SESSION_START_ORIGIN_ENSURE_MARKER_MASK |
        (request_id & LISTENER_AUDIO_SESSION_START_ORIGIN_ENSURE_REQUEST_MASK));
}

static bool voice_recording_control_source_is_ensure(const char *source)
{
    return source != NULL && strcmp(source, VOICE_RECORDING_CONTROL_ENSURE_SOURCE) == 0;
}

static void voice_recording_control_log_ensure_result(
    const char *phase,
    const char *source,
    esp_err_t result,
    const char *detail)
{
    ESP_LOGI(
        TAG,
        "ensure result phase=%s source=%s request_id=%" PRIX32
        " previous_segment_id=%" PRIX32 " actual_segment_id=%" PRIX32
        " status=%s result=%s detail=%s",
        phase != NULL ? phase : "unknown",
        source != NULL ? source : "unknown",
        s_ensure_request_id,
        s_ensure_previous_segment_id,
        s_ensure_actual_segment_id,
        voice_recording_control_ensure_status_name(s_ensure_status),
        esp_err_to_name(result),
        detail != NULL ? detail : "none");
}

static bool voice_recording_control_host_speech_protection_active(void)
{
    if (s_host_speech_protect_until_tick == 0 ||
        s_state != VOICE_RECORDING_STATE_RECORDING ||
        !s_active_session_visible) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    return (int32_t)(s_host_speech_protect_until_tick - now) > 0;
}

static void voice_recording_control_clear_host_speech_protection(void)
{
    s_host_speech_protect_until_tick = 0;
}

static void voice_recording_control_note_host_speech(void)
{
    if (s_state != VOICE_RECORDING_STATE_RECORDING ||
        !s_active_session_visible) {
        return;
    }

    s_active_session_body_speech_seen = true;
    if (!s_host_endpoint_ownership_logged) {
        s_host_endpoint_ownership_logged = true;
        if (!s_active_session_enrollment) {
            ESP_LOGI(
                TAG,
                "host endpoint ownership accepted session_count=%" PRIu32
                " source=VREC:SPEECH automatic=%u visible=%u",
                s_session_count,
                s_active_session_automatic ? 1u : 0u,
                s_active_session_visible ? 1u : 0u);
        }
    }

    denzic_voice_activation_v1_input_t input = {
        .elapsed_ms = 0,
        .enabled = s_voice_monitoring,
        .auto_start_enabled = s_voice_auto_start_enabled,
        .auto_stop_enabled = s_voice_auto_stop_enabled,
        .recording_active = true,
        .speech_detected = true,
        .start_blocked = true,
    };
    (void)denzic_voice_activation_v1_step(
        &s_voice_activation_machine,
        &s_voice_activation_config,
        &input);
    s_host_speech_protect_until_tick =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_HOST_SPEECH_PROTECTION_MS);
}

static const char *voice_recording_control_store_source(
    char *storage,
    size_t storage_bytes,
    const char *source)
{
    if (storage == NULL || storage_bytes == 0) {
        return "unknown";
    }
    snprintf(
        storage,
        storage_bytes,
        "%s",
        source != NULL ? source : "unknown");
    return storage;
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

static bool voice_recording_control_source_is_hidden_automatic(
    const char *source)
{
    return source != NULL && strstr(source, "voice_activation") != NULL;
}

static bool voice_recording_control_source_is_explicit_manual_control(
    const char *source)
{
    if (voice_recording_control_source_is_hidden_automatic(source)) {
        return false;
    }
    return voice_recording_control_source_is_host_control(source) ||
           voice_recording_control_source_is_user_start_intent(source);
}

static bool voice_recording_control_voice_diag_eligible(
    const char *source,
    uint32_t pre_roll_ms)
{
    /* Keep this qualification separate from the product source classifier:
     * voice_activation.auto_start is an automatic candidate even though its
     * source string shares the production "voice" prefix. The reproduced
     * failure is the explicit host toggle, and explicit key starts are also
     * safe diagnostic subjects when they have no hidden pre-roll. */
    if (pre_roll_ms != 0u || source == NULL) {
        return false;
    }
    return voice_recording_control_source_is_ble_audio_control(source) ||
           strncmp(source, "key1", strlen("key1")) == 0 ||
           strncmp(source, "ec11", strlen("ec11")) == 0;
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
        POWER_MANAGER_BLOCKER_RECORDING |
            POWER_MANAGER_BLOCKER_BLE_AUDIO |
            POWER_MANAGER_BLOCKER_VOICE_ACTIVATION,
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

static bool voice_recording_control_is_diag_state_command(const char *action)
{
    return action != NULL &&
           strncmp(
               action,
               VOICE_RECORDING_CONTROL_DIAG_STATE_PREFIX,
               strlen(VOICE_RECORDING_CONTROL_DIAG_STATE_PREFIX)) == 0;
}

static bool voice_recording_control_parse_diag_state_request(
    const char *action,
    uint32_t *request_id)
{
    if (!voice_recording_control_is_diag_state_command(action) || request_id == NULL) {
        return false;
    }

    const char *digits = action + strlen(VOICE_RECORDING_CONTROL_DIAG_STATE_PREFIX);
    size_t digit_count = strlen(digits);
    if (digit_count == 0u || digit_count > VOICE_RECORDING_CONTROL_DIAG_REQUEST_ID_MAX_DIGITS) {
        return false;
    }
    for (size_t index = 0; index < digit_count; ++index) {
        if (digits[index] < '0' || digits[index] > '9') {
            return false;
        }
    }

    uint32_t parsed = 0u;
    for (size_t index = 0; index < digit_count; ++index) {
        uint32_t digit = (uint32_t)(digits[index] - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }
    if (parsed == 0u) {
        return false;
    }
    *request_id = parsed;
    return true;
}

static void voice_recording_control_print_diag_state(uint32_t request_id)
{
    const bool audio_active = audio_capture_session_is_active();
    const bool audio_monitoring = audio_capture_voice_activation_monitoring_is_enabled();
    const bool transport_ready = ble_audio_stream_is_ready();
    const uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000LL);

    /* This function is called only by voice_recording_control_task after the
     * command queue has serialized the request with FSM mutations. The
     * cross-module getters are observations, not a claim of an atomic
     * snapshot across every subsystem. */
    printf(
        "~VREC:DIAG:STATE schema=listener.voice_recording_diag_state.v1"
        " result=OK request_id=%" PRIu32 " uptime_ms=%" PRIu64
        " firmware_version=%s build_id=%s settings_revision=%" PRIu32
        " state=%s pending_start=%u cancel_pending=%u enrollment_pending=%u"
        " session_count=%" PRIu32 " session_id=%" PRIu32
        " automatic=%u visible=%u enrollment=%u host_endpoint_owned=%u"
        " audio_active=%u monitoring=%u audio_monitoring=%u"
        " auto_start=%u auto_stop=%u transport_ready=%u\n",
        request_id,
        uptime_ms,
        listener_device_get_fw_version(),
        listener_device_get_build_id(),
        device_settings_get_revision(),
        voice_recording_state_name(s_state),
        s_pending_start ? 1u : 0u,
        s_cancel_pending ? 1u : 0u,
        s_enrollment_start_pending ? 1u : 0u,
        s_session_count,
        s_active_session_id,
        s_active_session_automatic ? 1u : 0u,
        s_active_session_visible ? 1u : 0u,
        s_active_session_enrollment ? 1u : 0u,
        s_active_session_host_endpoint_owned ? 1u : 0u,
        audio_active ? 1u : 0u,
        s_voice_monitoring ? 1u : 0u,
        audio_monitoring ? 1u : 0u,
        s_voice_auto_start_enabled ? 1u : 0u,
        s_voice_auto_stop_enabled ? 1u : 0u,
        transport_ready ? 1u : 0u);
    fflush(stdout);
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
            /* activity=NULL: Type VREC:STOP after hidden VA must not reset
             * the 5-minute low-power timer. */
            return voice_recording_control_make_decision(
                VOICE_RECORDING_EFFECT_STOP_CLEANUP,
                VOICE_RECORDING_STATE_IDLE,
                VOICE_RECORDING_FLOW_TOGGLE_IGNORED,
                ESP_OK,
                false,
                "host_cleanup_stop_completed",
                NULL,
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
            NULL,
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
    s_pending_start_owner = VOICE_RECORDING_PENDING_OWNER_NONE;
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
    const bool ensure_pending =
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION &&
        s_ensure_valid &&
        s_ensure_status == VOICE_RECORDING_ENSURE_STATUS_PENDING;
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
    if (ensure_pending) {
        s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_FAILED;
        s_ensure_deadline_tick = 0;
        voice_recording_control_log_ensure_result(
            "pending_canceled",
            source,
            ESP_ERR_INVALID_STATE,
            detail);
    }
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
    const char *detail,
    voice_recording_pending_owner_t owner)
{
    const bool pending_va =
        owner == VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE;
    if (pending_va && !power_manager_try_acquire_voice_activation()) {
        ESP_LOGI(TAG, "pending hidden voice candidate rejected at idle commit boundary");
        return;
    }

    TickType_t now = xTaskGetTickCount();
    s_pending_start = true;
    s_pending_start_owner = owner;
    s_pending_start_source = voice_recording_control_store_source(
        s_pending_start_source_storage,
        sizeof(s_pending_start_source_storage),
        source);
    s_pending_start_reason = reason != NULL ? reason : "audio_transport_not_ready";
    s_pending_start_detail = detail != NULL ? detail : "recording_waiting_for_ble_audio";
    const uint32_t pending_timeout_ms = owner == VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION
        ? VOICE_RECORDING_CONTROL_ENSURE_PENDING_TIMEOUT_MS
        : VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS;
    s_pending_start_deadline_tick = now + pdMS_TO_TICKS(pending_timeout_ms);
    s_pending_start_next_retry_tick = now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_PENDING_START_RETRY_MS);

    /* VA pending must not reset low-power idle clocks. */
    if (!pending_va) {
        power_manager_record_activity("voice_recording_wait_transport");
    }
    (void)ble_hid_gap_request_active_connection();
    /* Pending VA auto-start uses the non-sleep VOICE_ACTIVATION marker only. */
    if (!pending_va) {
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
            true);
    }
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    status_led_clear_error(STATUS_LED_ERROR_DOMAIN_REC);
    ESP_LOGW(
        TAG,
        "recording start pending source=%s timeout_ms=%u reason=%s",
        source,
        pending_timeout_ms,
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
    const uint32_t pending_timeout_ms =
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION
        ? VOICE_RECORDING_CONTROL_ENSURE_PENDING_TIMEOUT_MS
        : VOICE_RECORDING_CONTROL_PENDING_START_TIMEOUT_MS;
    const bool ensure_pending =
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION &&
        s_ensure_valid &&
        s_ensure_status == VOICE_RECORDING_ENSURE_STATUS_PENDING;
    voice_recording_control_reset_pending_start();
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NOT_AVAILABLE);
    status_led_set_error(STATUS_LED_ERROR_DOMAIN_REC, STATUS_LED_ERROR_RETRYABLE, "recording_start_transport_timeout");
    ESP_LOGW(
        TAG,
        "recording pending start timed out source=%s timeout_ms=%u reason=%s",
        source,
        pending_timeout_ms,
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
    if (ensure_pending) {
        s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_FAILED;
        s_ensure_deadline_tick = 0;
        voice_recording_control_log_ensure_result(
            "pending_timeout",
            source,
            reason,
            "transport_timeout");
    }
}

static esp_err_t voice_recording_control_enter_recording(
    const char *source,
    bool log_rejection,
    bool request_reconnect,
    uint32_t pre_roll_ms)
{
    const bool voice_diag_active = voice_stack_diag_enter(
        voice_recording_control_voice_diag_eligible(source, pre_roll_ms)
            ? 1u
            : 0u);
    if (voice_diag_active) {
        voice_stack_diag_mark(
            VOICE_STACK_DIAG_STAGE_CONTROL_ENTER,
            0u,
            voice_recording_source_code(source),
            (uint32_t)s_state);
    }
    power_manager_snapshot_t power = {0};
    power_manager_get_snapshot(&power);
    if ((power.blockers & POWER_MANAGER_BLOCKER_OTA) != 0u ||
        ble_audio_stream_type_ota_hold_is_active()) {
        denzic_voice_activation_v1_reset(&s_voice_activation_machine);
        ESP_LOGI(
            TAG,
            "recording start ignored during OTA source=%s",
            source != NULL ? source : "unknown");
        if (voice_diag_active) {
            voice_stack_diag_leave(ESP_ERR_NOT_ALLOWED);
        }
        return ESP_ERR_NOT_ALLOWED;
    }
    if (pre_roll_ms > 0u && !power_manager_try_acquire_voice_activation()) {
        ESP_LOGI(TAG, "hidden voice candidate rejected at idle commit boundary source=%s",
                 source != NULL ? source : "unknown");
        if (voice_diag_active) {
            voice_stack_diag_leave(ESP_ERR_NOT_ALLOWED);
        }
        return ESP_ERR_NOT_ALLOWED;
    }

    /* Hidden automatic (VAD) candidates must not look like user activity —
     * otherwise every ambient false-start restarts the low-power idle clock
     * and the configured 5-minute low-power never arrives. User key / host
     * starts (no pre-roll) still count as activity. */
    if (pre_roll_ms == 0u) {
        power_manager_record_activity("voice_recording_start");
    }
    (void)ble_hid_gap_request_active_connection();
    /* Hidden VA (pre_roll>0) must NOT set RECORDING|BLE_AUDIO or reset idle
     * clocks. Its atomically acquired VOICE_ACTIVATION blocker only drains the
     * in-flight candidate before idle commit. Visible/user recording still
     * holds the full awake blockers. */
    if (pre_roll_ms == 0u) {
        power_manager_set_blocker(
            POWER_MANAGER_BLOCKER_RECORDING | POWER_MANAGER_BLOCKER_BLE_AUDIO,
            true);
    }

    if (voice_diag_active) {
        voice_stack_diag_mark(
            VOICE_STACK_DIAG_STAGE_SESSION_BEGIN_BEFORE,
            0u,
            pre_roll_ms,
            0u);
    }
    uint16_t session_start_origin = pre_roll_ms > 0u
        ? LISTENER_AUDIO_SESSION_START_ORIGIN_VOICE_ACTIVATION
        : LISTENER_AUDIO_SESSION_START_ORIGIN_USER;
    if (voice_recording_control_source_is_ensure(source) && s_ensure_valid) {
        session_start_origin = voice_recording_control_ensure_start_origin(
            s_ensure_request_id);
    }
    esp_err_t ret = voice_recording_control_source_is_ensure(source)
        ? audio_capture_session_resume_wake(
            s_ensure_previous_segment_id, session_start_origin)
        : audio_capture_session_begin_with_preroll_and_origin(
            pre_roll_ms, session_start_origin);
    if (ret != ESP_OK) {
        if (voice_diag_active) {
            voice_stack_diag_mark(
                VOICE_STACK_DIAG_STAGE_SESSION_BEGIN_AFTER,
                0u,
                (uint32_t)ret,
                0u);
        }
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
        if (voice_diag_active) {
            voice_stack_diag_leave(ret);
        }
        return ret;
    }

    /* A successful explicit/user session is an independent start boundary.
     * It must not inherit a latch left by an earlier hidden candidate. */
    if (pre_roll_ms == 0u &&
        voice_recording_control_source_is_explicit_manual_control(source)) {
        voice_recording_control_clear_auto_rearm_latch(
            "explicit_recording_start");
    }
    voice_recording_control_reset_pending_start();
    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = voice_recording_control_store_source(
        s_active_session_source_storage,
        sizeof(s_active_session_source_storage),
        source);
    s_active_session_automatic = pre_roll_ms > 0u;
    s_active_session_id = audio_capture_session_id();
    s_active_session_visible = !s_active_session_automatic;
    s_active_session_body_speech_seen = false;
    s_active_session_host_endpoint_owned =
        voice_recording_control_source_is_ensure(source) && s_ensure_valid;
    s_host_endpoint_ownership_logged = false;
    s_active_session_enrollment = false;
    voice_recording_control_clear_host_speech_protection();
    __atomic_store_n(&s_vad_queue_drop_count, 0u, __ATOMIC_RELAXED);
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
             1,
             voice_recording_source_code(source),
             s_session_count,
              (s_active_session_automatic ? 1u : 0u) |
                  (s_active_session_visible ? 2u : 0u) |
                  (s_active_session_enrollment ? 4u : 0u));
    if (voice_diag_active) {
        voice_stack_diag_mark(
            VOICE_STACK_DIAG_STAGE_SESSION_BEGIN_AFTER,
            0u,
            (uint32_t)ret,
            0u);
        voice_stack_diag_set_session_active(true);
        voice_stack_diag_leave(ret);
    }
    return ESP_OK;
}

static esp_err_t voice_recording_control_exit_recording_with_origin(
    const char *source,
    audio_capture_stop_origin_t origin)
{
    const uint32_t session_flags =
        (s_active_session_automatic ? 1u : 0u) |
        (s_active_session_visible ? 2u : 0u) |
        (s_active_session_enrollment ? 4u : 0u);
    /* Only count stop as user activity for visible / non-automatic sessions.
     * Silent VA reject must not reset the low-power idle countdown. */
    if (!s_active_session_automatic || s_active_session_visible) {
        power_manager_record_activity("voice_recording_stop");
    }
    bool processing_feedback_allowed =
        !s_active_session_automatic || s_active_session_visible;
    esp_err_t ret = audio_capture_session_stop_with_origin(origin);
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            voice_stack_diag_set_session_active(false);
            s_state = VOICE_RECORDING_STATE_IDLE;
            s_cancel_pending = false;
            s_cancel_source = NULL;
            s_active_session_source = NULL;
            s_active_session_id = 0u;
            s_active_session_automatic = false;
            s_active_session_visible = false;
            s_active_session_host_endpoint_owned = false;
            s_active_session_enrollment = false;
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

    voice_stack_diag_set_session_active(false);
    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_state = VOICE_RECORDING_STATE_TRANSFERRING;
    s_active_session_visible = false;
    s_active_session_host_endpoint_owned = false;
    s_active_session_enrollment = false;
    voice_recording_control_clear_host_speech_protection();
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
             2, voice_recording_source_code(source), s_session_count, session_flags);
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
    const uint32_t ended_segment_id = s_active_session_id;
    /* Hidden VA end must NOT refresh user/radio idle clocks. Ambient rejects
     * every few seconds were zeroing the 5-minute CONNECTED_IDLE timer so low
     * power only "worked once" between quiet stretches (owner: 两次都只能进一次). */
    const bool hidden_va =
        s_active_session_automatic && !s_active_session_visible;
    if (!hidden_va) {
        power_manager_record_activity("voice_recording_stop_cleanup");
    } else {
        /* The audio drain is now complete, so no subsequent recording-active
         * VAD step can recreate cooldown state. Re-arm here rather than when
         * Type first sends STOP. Keep 400 ms cooldown so Type is not flooded
         * with overlapping rejects (owner: 预览出不来 / 还是不行). */
        denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    }
    s_state = VOICE_RECORDING_STATE_IDLE;
    s_cancel_pending = false;
    s_cancel_source = NULL;
    s_active_session_source = NULL;
    s_active_session_id = 0u;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    s_active_session_host_endpoint_owned = false;
    s_active_session_enrollment = false;
    voice_recording_control_clear_host_speech_protection();
    voice_recording_control_clear_power_blockers();
    (void)voice_key_input_set_recording_output(false);
    status_led_set_recording(false, STATUS_LED_REC_SOURCE_NONE);
    voice_recording_control_log_toggle_ignored(
        source,
        detail != NULL ? detail : "host_cleanup_stop_completed",
        ESP_OK,
        false);
    voice_recording_control_log_device_status("ready", "recording_session_cleanup");
    const bool ensure_successor_reserved =
        voice_recording_control_schedule_ensure_successor(
            ended_segment_id,
            source);
    if (!ensure_successor_reserved) {
        voice_recording_control_try_start_pending_enrollment("transfer_cleanup");
    }
}

static void voice_recording_control_stop(const char *source)
{
    if (s_state == VOICE_RECORDING_STATE_RECORDING &&
        s_active_session_enrollment &&
        voice_recording_control_source_is_ble_audio_control(source) &&
        !voice_recording_control_tick_reached(
            xTaskGetTickCount(),
            s_enrollment_stale_stop_guard_until_tick)) {
        ESP_LOGI(
            TAG,
            "stale hidden-candidate cleanup stop ignored during enrollment guard source=%s",
            source);
        voice_recording_control_log_toggle_ignored(
            source,
            "owner_enrollment_stale_cleanup_stop_ignored",
            ESP_OK,
            false);
        return;
    }
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_STOP, &snapshot);

    /* Never treat host STOP as activity while a hidden automatic candidate is
     * (or was) in flight — that is the ambient reject path from Type. */
    const bool hidden_va =
        s_active_session_automatic && !s_active_session_visible;
    if (decision.activity != NULL && !hidden_va) {
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
                    "recording_waiting_for_ble_audio",
                    VOICE_RECORDING_PENDING_OWNER_EXPLICIT_CONTROL);
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
                "recording_waiting_for_previous_session",
                VOICE_RECORDING_PENDING_OWNER_EXPLICIT_CONTROL);
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
    const uint32_t session_flags =
        (s_active_session_automatic ? 1u : 0u) |
        (s_active_session_visible ? 2u : 0u) |
        (s_active_session_enrollment ? 4u : 0u);
    if ((!s_active_session_automatic || s_active_session_visible) &&
        voice_recording_control_source_is_explicit_manual_control(source)) {
        voice_recording_control_clear_auto_rearm_latch("explicit_cancel");
    }
    s_enrollment_start_pending = false;
    s_enrollment_start_deadline_tick = 0;
    s_enrollment_start_next_retry_tick = 0;
    voice_recording_control_snapshot_t snapshot = voice_recording_control_make_snapshot(source);
    voice_recording_control_decision_t decision =
        voice_recording_control_decide_transition(VOICE_RECORDING_EVENT_CANCEL, &snapshot);

    if (decision.activity != NULL) {
        power_manager_record_activity(decision.activity);
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_CLEAR_PENDING_START) {
        voice_recording_control_cancel_pending_start(decision.detail);
        diag_log(DIAG_SRC_VOICE_REC, DIAG_VREC_SESSION, DIAG_SEV_INFO,
                 3, voice_recording_source_code(source), s_session_count, session_flags);
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
            s_active_session_id = 0u;
            s_active_session_automatic = false;
            s_active_session_visible = false;
            s_active_session_host_endpoint_owned = false;
            s_active_session_enrollment = false;
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
    s_cancel_source = voice_recording_control_store_source(
        s_cancel_source_storage,
        sizeof(s_cancel_source_storage),
        source);
    if (!audio_capture_session_is_active()) {
        s_cancel_pending = false;
        s_cancel_source = NULL;
        s_state = VOICE_RECORDING_STATE_IDLE;
        s_active_session_source = NULL;
        s_active_session_id = 0u;
        s_active_session_automatic = false;
        s_active_session_visible = false;
        s_active_session_host_endpoint_owned = false;
        s_active_session_enrollment = false;
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
                 3, voice_recording_source_code(source), s_session_count, session_flags);
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

static bool voice_recording_control_ensure_status_terminal(
    voice_recording_ensure_status_t status)
{
    return status == VOICE_RECORDING_ENSURE_STATUS_DONE ||
        status == VOICE_RECORDING_ENSURE_STATUS_ABORTED ||
        status == VOICE_RECORDING_ENSURE_STATUS_FAILED;
}

static esp_err_t voice_recording_control_ensure_failed(
    const char *source,
    esp_err_t result,
    const char *detail)
{
    s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_FAILED;
    s_ensure_deadline_tick = 0;
    voice_recording_control_log_ensure_result(
        "failed",
        source,
        result,
        detail);
    return result;
}

static bool voice_recording_control_ensure_pending_for_segment(
    uint32_t segment_id)
{
    return s_ensure_valid &&
        s_ensure_status == VOICE_RECORDING_ENSURE_STATUS_PENDING &&
        s_ensure_previous_segment_id == segment_id;
}

static bool voice_recording_control_ensure_deadline_reached(TickType_t now)
{
    return s_ensure_deadline_tick != 0 &&
        voice_recording_control_tick_reached(now, s_ensure_deadline_tick);
}

static void voice_recording_control_expire_ensure_reservation(
    const char *source,
    const char *detail)
{
    if (!s_ensure_valid ||
        s_ensure_status != VOICE_RECORDING_ENSURE_STATUS_PENDING) {
        return;
    }
    s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_FAILED;
    s_ensure_deadline_tick = 0;
    voice_recording_control_log_ensure_result(
        "expired",
        source,
        ESP_ERR_TIMEOUT,
        detail);
}

static bool voice_recording_control_schedule_ensure_successor(
    uint32_t previous_segment_id,
    const char *source)
{
    if (!voice_recording_control_ensure_pending_for_segment(
            previous_segment_id)) {
        return false;
    }

    TickType_t now = xTaskGetTickCount();
    if (voice_recording_control_ensure_deadline_reached(now)) {
        voice_recording_control_expire_ensure_reservation(
            source,
            "predecessor_stopped_after_deadline");
        return false;
    }

    voice_recording_control_schedule_pending_start(
        VOICE_RECORDING_CONTROL_ENSURE_SOURCE,
        "predecessor_session_finished",
        "wake_capture_continuation_pending",
        VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION);
    /* The host deadline starts when ENSURE is accepted.  Finishing the
     * predecessor must not grant a fresh three-second lease. */
    s_pending_start_deadline_tick = s_ensure_deadline_tick;
    s_pending_start_next_retry_tick = now;
    voice_recording_control_log_ensure_result(
        "pending",
        source,
        ESP_OK,
        "predecessor_stopped_successor_reserved");
    return true;
}

static void voice_recording_control_poll_ensure_reservation(void)
{
    if (!s_ensure_valid ||
        s_ensure_status != VOICE_RECORDING_ENSURE_STATUS_PENDING ||
        s_pending_start ||
        !voice_recording_control_ensure_deadline_reached(
            xTaskGetTickCount())) {
        return;
    }
    voice_recording_control_expire_ensure_reservation(
        VOICE_RECORDING_CONTROL_ENSURE_SOURCE,
        "predecessor_did_not_finish_before_deadline");
}

static esp_err_t voice_recording_control_ensure_wake_session(
    const char *action,
    const char *source)
{
    uint32_t request_id = 0u;
    uint32_t previous_segment_id = 0u;
    if (!voice_recording_control_parse_ensure_action(
            action,
            &request_id,
            &previous_segment_id)) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s action=%s reason=invalid_request",
            source,
            action != NULL ? action : "null");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ensure_cancelled_request_valid &&
        s_ensure_cancelled_request_id == request_id) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s request_id=%" PRIX32
            " reason=abort_seen_before_ensure",
            source,
            request_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ensure_request_watermark_valid &&
        request_id < s_ensure_request_watermark) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s request_id=%" PRIX32
            " watermark=%" PRIX32 " reason=stale_request",
            source,
            request_id,
            s_ensure_request_watermark);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ensure_valid &&
        s_ensure_request_id == request_id &&
        s_ensure_previous_segment_id == previous_segment_id) {
        voice_recording_control_log_ensure_result(
            "duplicate",
            source,
            ESP_OK,
            "same_request_idempotent_replay");
        return ESP_OK;
    }
    if (s_ensure_valid && s_ensure_request_id == request_id) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s request_id=%" PRIX32
            " previous_segment_id=%" PRIX32
            " active_previous_segment_id=%" PRIX32
            " reason=request_arguments_changed",
            source,
            request_id,
            previous_segment_id,
            s_ensure_previous_segment_id);
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ensure_valid &&
        !voice_recording_control_ensure_status_terminal(s_ensure_status)) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s request_id=%" PRIX32
            " previous_segment_id=%" PRIX32
            " active_request_id=%" PRIX32
            " active_previous_segment_id=%" PRIX32
            " status=%s reason=conflicting_in_flight_request",
            source,
            request_id,
            previous_segment_id,
            s_ensure_request_id,
            s_ensure_previous_segment_id,
            voice_recording_control_ensure_status_name(s_ensure_status));
        return ESP_ERR_INVALID_STATE;
    }

    if (s_active_session_enrollment || s_enrollment_start_pending) {
        ESP_LOGW(
            TAG,
            "ensure rejected source=%s request_id=%" PRIX32
            " reason=owner_enrollment_active",
            source,
            request_id);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pending_start &&
        s_pending_start_owner != VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE &&
        s_pending_start_owner != VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION) {
        return voice_recording_control_ensure_failed(
            source,
            ESP_ERR_INVALID_STATE,
            "another_pending_start");
    }

    /* An unqualified ambient start is replaceable work, not another owner.
     * Keep it until a continuation is reserved or source-qualified PCM really
     * starts. enter_recording resets it only on success; a stale or missing
     * source leaves the ambient request intact. Explicit control and enrollment
     * remain protected by the gates above. */

    s_ensure_valid = true;
    s_ensure_request_id = request_id;
    s_ensure_previous_segment_id = previous_segment_id;
    s_ensure_actual_segment_id = 0u;
    s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_PENDING;
    s_ensure_deadline_tick =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_ENSURE_PENDING_TIMEOUT_MS);
    s_ensure_request_watermark_valid = true;
    s_ensure_request_watermark = request_id;
    voice_recording_control_log_ensure_result(
        "accepted",
        source,
        ESP_OK,
        "request_received");

    if (s_state == VOICE_RECORDING_STATE_RECORDING) {
        const uint32_t actual_segment_id = s_active_session_id;
        if (actual_segment_id != previous_segment_id) {
            return voice_recording_control_ensure_failed(
                source,
                ESP_ERR_INVALID_STATE,
                "recording_segment_mismatch");
        }
        s_ensure_actual_segment_id = actual_segment_id;
        if (s_active_session_automatic || s_active_session_visible) {
            const bool was_visible = s_active_session_visible;
            esp_err_t ret = voice_recording_control_adopt_active_wake_session(
                VOICE_RECORDING_CONTROL_ENSURE_SOURCE);
            if (ret != ESP_OK) {
                voice_recording_control_cancel("ensure_marker_failed");
                return voice_recording_control_ensure_failed(
                    source,
                    ret,
                    "adopt_existing_capture_failed");
            }
            s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_ACTIVE;
            s_ensure_deadline_tick = 0;
            voice_recording_control_log_ensure_result(
                "active",
                source,
                ESP_OK,
                was_visible
                    ? "adopted_existing_visible"
                    : "promoted_existing_hidden");
            return ESP_OK;
        }
        return voice_recording_control_ensure_failed(
            source,
            ESP_ERR_INVALID_STATE,
            "recording_not_promotable");
    }

    if (s_state == VOICE_RECORDING_STATE_TRANSFERRING) {
        const uint32_t actual_segment_id = s_active_session_id;
        if (actual_segment_id != previous_segment_id) {
            return voice_recording_control_ensure_failed(
                source,
                ESP_ERR_INVALID_STATE,
                "transferring_segment_mismatch");
        }
        voice_recording_control_schedule_pending_start(
            VOICE_RECORDING_CONTROL_ENSURE_SOURCE,
            "audio_session_transferring",
            "wake_capture_continuation_pending",
            VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION);
        s_pending_start_deadline_tick = s_ensure_deadline_tick;
        voice_recording_control_log_ensure_result(
            "pending",
            source,
            ESP_OK,
            "previous_session_transferring");
        return ESP_OK;
    }

    if (s_state != VOICE_RECORDING_STATE_IDLE) {
        return voice_recording_control_ensure_failed(
            source,
            ESP_ERR_INVALID_STATE,
            "recording_state_not_startable");
    }

    esp_err_t ret = voice_recording_control_enter_recording(
        VOICE_RECORDING_CONTROL_ENSURE_SOURCE,
        true,
        true,
        0u);
    if (ret == ESP_OK) {
        s_ensure_actual_segment_id = s_active_session_id;
        s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_ACTIVE;
        s_ensure_deadline_tick = 0;
        voice_recording_control_log_ensure_result(
            "active",
            source,
            ESP_OK,
            "started_new_visible_continuation");
        return ESP_OK;
    }
    if (ret == ESP_ERR_INVALID_STATE && !audio_capture_session_is_active()) {
        voice_recording_control_schedule_pending_start(
            VOICE_RECORDING_CONTROL_ENSURE_SOURCE,
            "audio_transport_not_ready",
            "wake_capture_continuation_pending",
            VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION);
        s_pending_start_deadline_tick = s_ensure_deadline_tick;
        voice_recording_control_log_ensure_result(
            "pending",
            source,
            ESP_OK,
            "transport_not_ready");
        return ESP_OK;
    }
    return voice_recording_control_ensure_failed(
        source,
        ret,
        "new_visible_continuation_start_failed");
}

static esp_err_t voice_recording_control_abort_wake_session(
    const char *action,
    const char *source)
{
    uint32_t request_id = 0u;
    if (!voice_recording_control_parse_abort_action(action, &request_id)) {
        ESP_LOGW(
            TAG,
            "ensure abort rejected source=%s action=%s reason=invalid_request",
            source,
            action != NULL ? action : "null");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ensure_request_watermark_valid &&
        request_id < s_ensure_request_watermark) {
        ESP_LOGI(
            TAG,
            "ensure abort ignored source=%s request_id=%" PRIX32
            " watermark=%" PRIX32 " reason=stale_request",
            source,
            request_id,
            s_ensure_request_watermark);
        return ESP_OK;
    }
    if (!s_ensure_valid || s_ensure_request_id != request_id) {
        // Remember an early ABORT so a delayed ENSURE with the same request
        // id cannot resurrect a transaction that the host already abandoned.
        s_ensure_cancelled_request_valid = true;
        s_ensure_cancelled_request_id = request_id;
        ESP_LOGI(
            TAG,
            "ensure abort recorded source=%s request_id=%" PRIX32
            " reason=abort_before_ensure",
            source,
            request_id);
        return ESP_OK;
    }
    if (voice_recording_control_ensure_status_terminal(s_ensure_status)) {
        voice_recording_control_log_ensure_result(
            "abort_duplicate",
            source,
            ESP_OK,
            "terminal_request_not_revived");
        return ESP_OK;
    }

    s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_ABORTED;
    s_ensure_deadline_tick = 0;
    const bool pending_ensure =
        s_pending_start &&
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_WAKE_CONTINUATION;
    const bool active_matching_segment =
        (s_state == VOICE_RECORDING_STATE_RECORDING ||
         s_state == VOICE_RECORDING_STATE_TRANSFERRING) &&
        (s_active_session_id == s_ensure_previous_segment_id ||
         (s_ensure_actual_segment_id != 0u &&
          s_active_session_id == s_ensure_actual_segment_id));
    if (pending_ensure) {
        voice_recording_control_cancel_pending_start("ensure_abort");
    }
    if (active_matching_segment) {
        voice_recording_control_cancel("ensure_wake_capture_abort");
    }
    voice_recording_control_log_ensure_result(
        "aborted",
        source,
        ESP_OK,
        pending_ensure || active_matching_segment
            ? "abort_applied"
            : "no_matching_active_capture");
    return ESP_OK;
}

static void voice_recording_control_host_sync(const char *source)
{
    /*
     * A fresh TYPE:READY can arrive after Type was closed while autonomous VAD
     * already opened a hidden stream. The new host has no START boundary for
     * that stream and otherwise begins with orphan PCM / a partial STOP. Reset
     * only recording state here; pairing identity and bonds are untouched.
     */
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    if (s_vad_queue != NULL) {
        xQueueReset(s_vad_queue);
    }
    voice_recording_control_cancel(source);
    // TYPE:READY starts a new host transaction epoch.  Cancel any physical
    // capture first, then forget request watermarks so a restarted Type
    // process may safely allocate ids from 1 again.
    s_ensure_valid = false;
    s_ensure_request_id = 0u;
    s_ensure_previous_segment_id = 0u;
    s_ensure_actual_segment_id = 0u;
    s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_NONE;
    s_ensure_deadline_tick = 0;
    s_ensure_cancelled_request_valid = false;
    s_ensure_request_watermark_valid = false;
    s_ensure_request_watermark = 0u;
    ESP_LOGI(
        TAG,
        "fresh Type host synchronized recording boundary source=%s",
        source != NULL ? source : "unknown");
}

static esp_err_t voice_recording_control_recovery(
    const char *source,
    bool type_controlled,
    bool suppress_swift_pair_prompt,
    bool manual_pairing_visual,
    bool force_fresh_identity)
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
        return ESP_OK;
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
        "recovery requested source=%s type_controlled=%u suppress_swift_pair=%u force_fresh_identity=%u: preparing BLE pairing reset",
        source,
        type_controlled ? 1u : 0u,
        suppress_swift_pair_prompt ? 1u : 0u,
        force_fresh_identity ? 1u : 0u);
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
    s_active_session_id = 0u;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    s_active_session_host_endpoint_owned = false;
    s_active_session_enrollment = false;
    s_enrollment_start_pending = false;
    s_enrollment_start_deadline_tick = 0;
    s_enrollment_start_next_retry_tick = 0;
    esp_err_t ret = ec11_fast_idle_recovery
        ? fast_recovery_ret
        : (force_fresh_identity
            ? ble_hid_gap_forget_bonds_and_repair_type_controlled_silent_fresh_identity()
            : suppress_swift_pair_prompt
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
        return ret;
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
    return ESP_OK;
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
    return voice_recording_control_adopt_active_wake_session(source);
}

static esp_err_t voice_recording_control_adopt_active_wake_session(
    const char *source)
{
    if (s_state != VOICE_RECORDING_STATE_RECORDING ||
        s_active_session_enrollment ||
        (!s_active_session_automatic && !s_active_session_visible)) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * A marker acknowledges ownership of the existing physical stream. It
     * does not reset the audio engine, packet sequence or pre-roll. In
     * particular, a visible predecessor must not wait for its own STOP to
     * complete ENSURE: accepted dictation is no longer firmware-endpointed.
     * Queue the marker before publishing the ownership change.
     */
    if (voice_recording_control_source_is_ensure(source) && s_ensure_valid) {
        const uint32_t actual_segment_id = s_active_session_id;
        esp_err_t marker_ret = ble_audio_stream_send_session_start_marker(
            actual_segment_id,
            voice_recording_control_ensure_start_origin(s_ensure_request_id));
        if (marker_ret != ESP_OK) {
            ESP_LOGW(
                TAG,
                "recording ENSURE marker rejected source=%s"
                " request_id=%" PRIX32 " actual_segment_id=%" PRIu32
                " ret=%s",
                source,
                s_ensure_request_id,
                actual_segment_id,
                esp_err_to_name(marker_ret));
            return marker_ret;
        }
        ESP_LOGI(
            TAG,
            "recording ENSURE marker queued source=%s"
            " request_id=%" PRIX32 " actual_segment_id=%" PRIu32,
            source,
            s_ensure_request_id,
            actual_segment_id);
    }
    s_active_session_host_endpoint_owned = true;
    if (s_active_session_visible) {
        return ESP_OK;
    }
    /* Only endpoint metadata belongs to the old wake window. The command
     * actor handles adoption before draining VAD, so discard queued hidden
     * deltas rather than replaying them as visible-session elapsed time. */
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    if (s_vad_queue != NULL) {
        xQueueReset(s_vad_queue);
    }
    s_active_session_visible = true;
    s_active_session_body_speech_seen = false;
    s_host_endpoint_ownership_logged = false;
    /* Accepted wake / promote: now it is real dictation — count activity. */
    power_manager_record_activity("automatic_candidate_accepted");
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

static esp_err_t voice_recording_control_start_enrollment(const char *source)
{
    const bool pending_retry =
        s_enrollment_start_pending &&
        strcmp(source, "owner_enrollment.pending") == 0;
    const TickType_t pending_deadline = s_enrollment_start_deadline_tick;
    if (s_pending_start && s_state == VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_cancel_pending_start(
            "owner_enrollment_supersedes_pending_start");
    }

    const bool hidden_automatic =
        s_active_session_automatic && !s_active_session_visible;
    if (hidden_automatic &&
        (s_state == VOICE_RECORDING_STATE_RECORDING ||
         s_state == VOICE_RECORDING_STATE_TRANSFERRING)) {
        s_enrollment_start_pending = true;
        TickType_t now = xTaskGetTickCount();
        s_enrollment_start_deadline_tick =
            now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_ENROLLMENT_START_TIMEOUT_MS);
        s_enrollment_start_next_retry_tick = now;
        ESP_LOGI(
            TAG,
            "owner enrollment queued behind hidden automatic candidate source=%s state=%s",
            source,
            voice_recording_state_name(s_state));
        if (s_state == VOICE_RECORDING_STATE_TRANSFERRING) {
            return ESP_OK;
        }
        esp_err_t stop_ret = voice_recording_control_exit_recording_with_origin(
            "owner_enrollment_preempt_hidden",
            AUDIO_CAPTURE_STOP_ORIGIN_USER);
        if (stop_ret != ESP_OK) {
            s_enrollment_start_pending = false;
            s_enrollment_start_deadline_tick = 0;
            s_enrollment_start_next_retry_tick = 0;
        }
        return stop_ret;
    }

    if (s_state != VOICE_RECORDING_STATE_IDLE ||
        s_pending_start ||
        s_cancel_pending ||
        audio_capture_session_is_active()) {
        ESP_LOGW(
            TAG,
            "owner enrollment start rejected source=%s state=%s pending=%u cancel=%u",
            source,
            voice_recording_state_name(s_state),
            s_pending_start ? 1u : 0u,
            s_cancel_pending ? 1u : 0u);
        return ESP_ERR_INVALID_STATE;
    }

    s_enrollment_start_pending = false;
    s_enrollment_start_deadline_tick = 0;
    s_enrollment_start_next_retry_tick = 0;
    esp_err_t ret = voice_recording_control_enter_recording(source, true, true, 0u);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE &&
            s_state == VOICE_RECORDING_STATE_IDLE &&
            !audio_capture_session_is_active()) {
            TickType_t now = xTaskGetTickCount();
            s_enrollment_start_pending = true;
            s_enrollment_start_deadline_tick = pending_retry
                ? pending_deadline
                : now + pdMS_TO_TICKS(
                    VOICE_RECORDING_CONTROL_ENROLLMENT_START_TIMEOUT_MS);
            s_enrollment_start_next_retry_tick =
                now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_ENROLLMENT_START_RETRY_MS);
            ESP_LOGI(
                TAG,
                "owner enrollment waiting for audio transport source=%s",
                source);
            return ESP_OK;
        }
        return ret;
    }

    s_active_session_enrollment = true;
    s_enrollment_stale_stop_guard_until_tick =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_ENROLLMENT_STALE_STOP_GUARD_MS);
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    if (s_vad_queue != NULL) {
        xQueueReset(s_vad_queue);
    }
    ESP_LOGI(
        TAG,
        "owner enrollment recording started; host owns bounded stop source=%s",
        source);
    voice_recording_control_log_device_status(
        "recording",
        "owner_enrollment_capture_active");
    return ESP_OK;
}

static void voice_recording_control_try_start_pending_enrollment(const char *context)
{
    if (!s_enrollment_start_pending) {
        return;
    }
    TickType_t now = xTaskGetTickCount();
    if (voice_recording_control_tick_reached(
            now,
            s_enrollment_start_deadline_tick)) {
        s_enrollment_start_pending = false;
        s_enrollment_start_deadline_tick = 0;
        s_enrollment_start_next_retry_tick = 0;
        ESP_LOGW(
            TAG,
            "pending owner enrollment start timed out context=%s state=%s audio_active=%u ble_ready=%u",
            context,
            voice_recording_state_name(s_state),
            audio_capture_session_is_active() ? 1u : 0u,
            ble_audio_stream_is_ready() ? 1u : 0u);
        voice_recording_control_log_device_error(
            "error",
            "owner_enrollment_pending_start_timeout",
            ESP_ERR_TIMEOUT);
        return;
    }
    if (s_state != VOICE_RECORDING_STATE_IDLE ||
        s_pending_start ||
        audio_capture_session_is_active() ||
        !ble_audio_stream_is_ready() ||
        !voice_recording_control_tick_reached(
            now,
            s_enrollment_start_next_retry_tick)) {
        return;
    }
    s_enrollment_start_next_retry_tick =
        now + pdMS_TO_TICKS(VOICE_RECORDING_CONTROL_ENROLLMENT_START_RETRY_MS);
    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    if (s_vad_queue != NULL) {
        xQueueReset(s_vad_queue);
    }
    esp_err_t ret = voice_recording_control_start_enrollment(
        "owner_enrollment.pending");
    if (ret != ESP_OK) {
        s_enrollment_start_pending = true;
        ESP_LOGW(
            TAG,
            "pending owner enrollment start failed context=%s state=%s: %s",
            context,
            voice_recording_state_name(s_state),
            esp_err_to_name(ret));
    }
}

static esp_err_t voice_recording_control_handle_control_command(
    const char *command,
    const char *source)
{
    const char *action = command;
    if (strncmp(
            command,
            VOICE_RECORDING_CONTROL_VREC_PREFIX,
            strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX)) == 0) {
        action = command + strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX);
    }

    if (voice_recording_control_is_diag_state_command(action)) {
        uint32_t request_id = 0u;
        if (!voice_recording_control_parse_diag_state_request(action, &request_id)) {
            ESP_LOGW(TAG, "drop diagnostic state query: invalid request");
            printf(
                "~VREC:DIAG:STATE schema=listener.voice_recording_diag_state.v1"
                " result=ERROR request_id=0 reason=invalid_request\n");
            fflush(stdout);
            return ESP_ERR_INVALID_ARG;
        }
        voice_recording_control_print_diag_state(request_id);
        return ESP_OK;
    }

    if (strcmp(action, "TOGGLE") == 0) {
        voice_recording_control_toggle(source);
        return ESP_OK;
    }
    if (strcmp(action, "CANCEL") == 0) {
        voice_recording_control_cancel(source);
        return ESP_OK;
    }
    if (strcmp(action, "HOST_SYNC") == 0) {
        voice_recording_control_host_sync(source);
        return ESP_OK;
    }
    if (strcmp(action, "ACTIVATE") == 0) {
        return voice_recording_control_activate_automatic_session(source);
    }
    if (strncmp(action, "ENSURE:", strlen("ENSURE:")) == 0) {
        return voice_recording_control_ensure_wake_session(action, source);
    }
    if (strncmp(action, "ABORT:", strlen("ABORT:")) == 0) {
        return voice_recording_control_abort_wake_session(action, source);
    }
    if (strcmp(action, "ENROLL") == 0) {
        return voice_recording_control_start_enrollment(source);
    }
    if (strcmp(action, "SPEECH") == 0) {
        voice_recording_control_note_host_speech();
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:START") == 0 || strcmp(action, "PROCESSING_START") == 0) {
        voice_recording_control_host_processing_start(source);
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:STOP") == 0 || strcmp(action, "PROCESSING_STOP") == 0) {
        voice_recording_control_host_processing_stop(source);
        return ESP_OK;
    }
    if (strcmp(action, "PROCESSING:DONE") == 0 || strcmp(action, "PROCESSING_DONE") == 0) {
        voice_recording_control_host_processing_done(source);
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
        return ESP_OK;
    }
    if (strcmp(action, "STOP") == 0 || strcmp(action, "CLEANUP") == 0) {
        voice_recording_control_stop(source);
        return ESP_OK;
    }
    if (strcmp(action, "RECOVERY:TYPE:MANUAL") == 0) {
        return voice_recording_control_recovery(source, true, false, true, false);
    }
    if (strcmp(action, "RECOVERY:TYPE:SILENT:FRESH") == 0 ||
        strcmp(action, "RECOVERY_TYPE_SILENT_FRESH") == 0) {
        return voice_recording_control_recovery(source, true, true, false, true);
    }
    if (strcmp(action, "RECOVERY:TYPE:SILENT") == 0 ||
        strcmp(action, "RECOVERY_TYPE_SILENT") == 0 ||
        strcmp(action, "RECOVERY:TYPE_NO_PROMPT") == 0 ||
        strcmp(action, "RECOVERY_TYPE_NO_PROMPT") == 0) {
        return voice_recording_control_recovery(source, true, true, false, false);
    }
    if (strcmp(action, "RECOVERY:TYPE") == 0 || strcmp(action, "RECOVERY_TYPE") == 0) {
        return voice_recording_control_recovery(source, true, false, false, false);
    }
    if (strcmp(action, "RECOVERY") == 0 || strcmp(action, "RESET") == 0 || strcmp(action, "FORGET") == 0) {
        return voice_recording_control_recovery(source, false, false, false, false);
    }

    ESP_LOGW(TAG, "drop control command source=%s command=%s", source, command);
    voice_recording_control_log_device_error("error", "unknown_control_command", ESP_ERR_INVALID_ARG);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t voice_recording_control_dispatch_control_command(const char *command, const char *source)
{
    if (command == NULL || source == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_command_queue == NULL || s_task_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    voice_recording_control_command_t queued = {0};
    size_t command_length = strnlen(
        command,
        VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES);
    size_t source_length = strnlen(
        source,
        VOICE_RECORDING_CONTROL_SOURCE_BYTES);
    if (command_length == 0 ||
        command_length >= VOICE_RECORDING_CONTROL_COMMAND_BUFFER_BYTES ||
        source_length == 0 ||
        source_length >= VOICE_RECORDING_CONTROL_SOURCE_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(queued.command, command, command_length);
    memcpy(queued.source, source, source_length);

    if (xQueueSend(s_command_queue, &queued, 0) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "control queue full source=%s command=%s",
            source,
            command);
        voice_recording_control_log_device_error(
            "error",
            "control_queue_full",
            ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    xTaskNotifyGive(s_task_handle);
    return ESP_OK;
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

    /* Type sends VREC:STOP after nearly every ambient reject. Counting that as
     * user/radio activity zeroed the 5-minute CONNECTED_IDLE clock forever
     * while voice_auto_start was on (owner: 两次都只能进一次低功耗). Only
     * user-intent control should refresh idle clocks. */
    const char *action = command;
    if (strncmp(
            command,
            VOICE_RECORDING_CONTROL_VREC_PREFIX,
            strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX)) == 0) {
        action = command + strlen(VOICE_RECORDING_CONTROL_VREC_PREFIX);
    }
    const bool user_intent_control =
        strcmp(action, "TOGGLE") == 0 ||
        strcmp(action, "ACTIVATE") == 0 ||
        strcmp(action, "ENROLL") == 0 ||
        strncmp(action, "RECOVERY", 8) == 0 ||
        strcmp(action, "RESET") == 0 ||
        strcmp(action, "FORGET") == 0;
    if (user_intent_control) {
        power_manager_record_activity("voice_recording_ble_control");
    }
    return voice_recording_control_dispatch_control_command(command, source);
}

static void voice_recording_control_poll_pending_start(void)
{
    if (!s_pending_start) {
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

    if (s_state == VOICE_RECORDING_STATE_TRANSFERRING) {
        return;
    }

    if (s_state != VOICE_RECORDING_STATE_IDLE) {
        voice_recording_control_reset_pending_start();
        return;
    }

    const bool pending_automatic =
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE;
    if (pending_automatic && s_voice_auto_rearm_await_quiet_edge) {
        voice_recording_control_cancel_pending_start(
            "voice_activation.auto_rearm_latched");
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
    /* VA auto-start pending must keep pre_roll so the session stays a hidden
     * automatic candidate (pre_roll==0 promotes to visible user recording). */
    const uint32_t pending_pre_roll_ms =
        pending_automatic
            ? s_voice_activation_config.pre_roll_ms
            : 0u;
    esp_err_t ret =
        voice_recording_control_enter_recording(source, false, false, pending_pre_roll_ms);
    if (ret == ESP_OK) {
        if (s_active_session_host_endpoint_owned &&
            s_ensure_valid &&
            s_ensure_status == VOICE_RECORDING_ENSURE_STATUS_PENDING) {
            s_ensure_actual_segment_id = s_active_session_id;
            s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_ACTIVE;
            s_ensure_deadline_tick = 0;
            voice_recording_control_log_ensure_result(
                "active",
                source,
                ESP_OK,
                "started_pending_continuation");
        }
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
    const uint32_t ended_segment_id = s_active_session_id;
    const bool hidden_automatic =
        s_active_session_automatic && !s_active_session_visible;
    if (voice_recording_control_ensure_pending_for_segment(
            ended_segment_id) &&
        s_cancel_pending) {
        s_ensure_status = VOICE_RECORDING_ENSURE_STATUS_ABORTED;
        s_ensure_deadline_tick = 0;
        voice_recording_control_log_ensure_result(
            "aborted",
            s_cancel_source != NULL ? s_cancel_source : "cancel",
            ESP_OK,
            "predecessor_cancelled_before_successor");
    }
    if (s_ensure_valid &&
        s_ensure_status == VOICE_RECORDING_ENSURE_STATUS_ACTIVE) {
        s_ensure_status = s_cancel_pending
            ? VOICE_RECORDING_ENSURE_STATUS_ABORTED
            : VOICE_RECORDING_ENSURE_STATUS_DONE;
        s_ensure_deadline_tick = 0;
        voice_recording_control_log_ensure_result(
            s_cancel_pending ? "aborted" : "done",
            s_cancel_pending
                ? (s_cancel_source != NULL ? s_cancel_source : "cancel")
                : (s_active_session_source != NULL ? s_active_session_source : "session"),
            ESP_OK,
            s_cancel_pending
                ? "capture_inactive_after_cancel"
                : "capture_inactive_after_start");
    }
    const uint32_t session_flags =
        (s_active_session_automatic ? 1u : 0u) |
        (s_active_session_visible ? 2u : 0u) |
        (s_active_session_enrollment ? 4u : 0u);
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
        if (!hidden_automatic &&
            voice_recording_control_source_is_explicit_manual_control(source)) {
            voice_recording_control_clear_auto_rearm_latch("finish_cancel");
        }
        s_cancel_pending = false;
        s_cancel_source = NULL;
        s_state = decision.next_state;
        s_active_session_source = NULL;
        s_active_session_id = 0u;
        s_active_session_automatic = false;
        s_active_session_visible = false;
        s_active_session_host_endpoint_owned = false;
        s_active_session_enrollment = false;
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
                 3, voice_recording_source_code(source), s_session_count, session_flags);
        /* Release the final VA/audio owner only after every session resource
         * and observable output is quiescent. Releasing it earlier can commit
         * CONNECTED_IDLE re-entrantly from power_manager_set_blocker() while
         * this teardown is still running. */
        voice_recording_control_clear_power_blockers();
        voice_recording_control_try_start_pending_enrollment("finish_cancel");
        return;
    }

    const char *source = s_active_session_source != NULL ? s_active_session_source : "session";
    s_state = decision.next_state;
    s_active_session_source = NULL;
    s_active_session_id = 0u;
    s_active_session_automatic = false;
    s_active_session_visible = false;
    s_active_session_host_endpoint_owned = false;
    s_active_session_enrollment = false;
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
        voice_recording_control_clear_power_blockers();
        const bool ensure_successor_reserved =
            voice_recording_control_schedule_ensure_successor(
                ended_segment_id,
                source);
        if (!ensure_successor_reserved) {
            voice_recording_control_try_start_pending_enrollment("finish_transfer");
        }
        return;
    }

    if (decision.effect == VOICE_RECORDING_EFFECT_FINISH_ABORT) {
        if (!hidden_automatic &&
            voice_recording_control_source_is_explicit_manual_control(source)) {
            voice_recording_control_clear_auto_rearm_latch("finish_abort");
        }
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
        if (voice_recording_control_ensure_pending_for_segment(
                ended_segment_id)) {
            voice_recording_control_ensure_failed(
                source,
                ESP_ERR_INVALID_STATE,
                "predecessor_aborted_before_successor");
        }
        voice_recording_control_clear_power_blockers();
        voice_recording_control_try_start_pending_enrollment("finish_abort");
        return;
    }

    voice_recording_control_clear_power_blockers();
}

static void voice_recording_control_on_voice_activity(
    bool speech_detected,
    uint32_t elapsed_ms)
{
    if (s_vad_queue == NULL) {
        return;
    }
    __atomic_fetch_add(&s_vad_event_total, 1u, __ATOMIC_RELAXED);
    voice_recording_control_vad_event_t event = {
        .speech_detected = speech_detected,
        .elapsed_ms = elapsed_ms,
    };
    if (xQueueSend(s_vad_queue, &event, 0) != pdTRUE) {
        uint32_t drops = __atomic_add_fetch(
            &s_vad_queue_drop_count,
            1u,
            __ATOMIC_RELAXED);
        if (drops == 1u || (drops & (drops - 1u)) == 0u) {
            ESP_LOGW(
                TAG,
                "VAD queue full drops=%" PRIu32 " depth=%u",
                drops,
                (unsigned)uxQueueMessagesWaiting(s_vad_queue));
        }
        return;
    }
    if (s_task_handle != NULL) {
        xTaskNotifyGive(s_task_handle);
    }
}

static void voice_recording_control_cancel_hidden_candidate_for_idle(
    const power_manager_snapshot_t *power)
{
    if (power == NULL || power->state == POWER_MANAGER_STATE_ACTIVE) {
        return;
    }

    const bool pending_automatic =
        s_pending_start &&
        s_pending_start_owner == VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE;
    if (pending_automatic) {
        denzic_voice_activation_v1_reset(&s_voice_activation_machine);
        if (s_vad_queue != NULL) {
            xQueueReset(s_vad_queue);
        }
        voice_recording_control_cancel_pending_start(
            "voice_activation_idle_pending_canceled");
    }

    const bool hidden_automatic =
        s_state == VOICE_RECORDING_STATE_RECORDING &&
        s_active_session_automatic &&
        !s_active_session_visible;
    if (!hidden_automatic || s_cancel_pending) {
        return;
    }

    denzic_voice_activation_v1_reset(&s_voice_activation_machine);
    if (s_vad_queue != NULL) {
        xQueueReset(s_vad_queue);
    }
    esp_err_t ret = audio_capture_session_cancel();
    if (ret != ESP_OK) {
        if (!audio_capture_session_is_active()) {
            voice_recording_control_complete_transfer_cleanup(
                "voice_activation.idle_suspend",
                "voice_activation_idle_session_inactive");
        } else {
            ESP_LOGW(
                TAG,
                "hidden automatic candidate idle cancel failed power=%s ret=%s",
                power_manager_state_name(power->state),
                esp_err_to_name(ret));
        }
        return;
    }

    s_cancel_pending = true;
    s_cancel_source = "voice_activation.idle_suspend";
    ESP_LOGI(
        TAG,
        "hidden automatic candidate canceled for idle power=%s",
        power_manager_state_name(power->state));
    voice_recording_control_log_flow(
        VOICE_RECORDING_FLOW_CANCEL,
        "voice_activation_idle_cancel_requested",
        s_cancel_source,
        ESP_OK,
        false);
    if (!audio_capture_session_is_active()) {
        voice_recording_control_handle_session_inactive();
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
    if (!s_voice_auto_start_enabled) {
        voice_recording_control_clear_auto_rearm_latch("auto_start_disabled");
    }
    if (!s_voice_auto_start_enabled &&
        s_active_session_automatic &&
        s_state == VOICE_RECORDING_STATE_RECORDING &&
        !s_cancel_pending) {
        denzic_voice_activation_v1_reset(
            &s_voice_activation_machine);
        voice_recording_control_cancel(
            "voice_activation.auto_start_disabled");
    }
    /* Voice wake is a non-idle, Bluetooth-ready feature. The normal
     * Bluetooth-light-off idle state may stop I2S/VAD/KWS (LST-WAKE-012). */
    bool idle_start_monitoring =
        (s_state == VOICE_RECORDING_STATE_IDLE ||
         s_state == VOICE_RECORDING_STATE_TRANSFERRING) &&
        /* Tail draining temporarily makes the stream busy, not disconnected.
         * Keep capturing the rolling wake prefix across this boundary. Turning
         * monitoring off allows audio idle to stop I2S and clear that prefix,
         * losing a wake spoken while the preceding candidate drains. Starting
         * a new session still requires the ordinary stream-ready gate below. */
        ble_audio_stream_is_type_link_ready() &&
        s_voice_auto_start_enabled;
    bool active_stop_monitoring =
        s_state == VOICE_RECORDING_STATE_RECORDING &&
        !s_cancel_pending &&
        !s_active_session_enrollment &&
        (s_active_session_automatic || s_voice_auto_stop_enabled);
    bool ota_active =
        (power.blockers & POWER_MANAGER_BLOCKER_OTA) != 0u ||
        ble_audio_stream_type_ota_hold_is_active();
    bool monitoring =
        power.state == POWER_MANAGER_STATE_ACTIVE &&
        !ota_active &&
        (idle_start_monitoring || active_stop_monitoring);
    /* audio_capture_set_idle_power_save(true) clears the capture-layer flag
     * without updating s_voice_monitoring. If we only compare the software
     * mirror, monitoring stays "on" here while the mic path is actually off
     * after idle — classic "works once, dead after idle/restart". */
    const bool audio_monitoring =
        audio_capture_voice_activation_monitoring_is_enabled();
    if (monitoring == s_voice_monitoring && monitoring == audio_monitoring) {
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

static void voice_recording_control_apply_power_state_change(void)
{
    power_manager_snapshot_t power = {0};
    power_manager_get_snapshot(&power);
    /* A hidden VAD candidate is not user-visible work. If it races the
     * Bluetooth-light-off boundary, cancel it without recording activity so
     * the mic can finish draining and enter idle power save. */
    voice_recording_control_cancel_hidden_candidate_for_idle(&power);
    /* A power callback is not automatically a new VAD episode: BLE/power can
     * cross a short boundary while monitoring and audio are still alive. Only
     * clear the guard once the old capture/monitoring episode is confirmed
     * quiescent; the following ACTIVE refresh then opens a clean episode. */
    const bool capture_quiescent =
        s_state == VOICE_RECORDING_STATE_IDLE &&
        !s_pending_start &&
        !s_cancel_pending &&
        !audio_capture_session_is_active();
    const bool vad_episode_ended =
        !s_voice_monitoring &&
        !audio_capture_voice_activation_monitoring_is_enabled();
    if (capture_quiescent && vad_episode_ended) {
        voice_recording_control_clear_auto_rearm_latch(
            "power_state_boundary");
    }
    /* Clear VA cooldown/speech residue at every power boundary. Entering idle
     * disables monitoring; a later physical/connection recovery starts clean. */
    if (s_state == VOICE_RECORDING_STATE_IDLE && !s_pending_start) {
        denzic_voice_activation_v1_reset(&s_voice_activation_machine);
        if (s_vad_queue != NULL) {
            xQueueReset(s_vad_queue);
        }
    }
    /* Force a monitoring re-apply because audio idle can change the capture
     * layer independently of this component's software mirror. */
    s_voice_monitoring = !s_voice_monitoring;
    voice_recording_control_refresh_voice_monitoring();
}

void voice_recording_control_on_power_state_changed(void)
{
    if (!s_started || s_task_handle == NULL) {
        return;
    }
    __atomic_store_n(
        &s_power_state_refresh_pending,
        true,
        __ATOMIC_RELEASE);
    xTaskNotifyGive(s_task_handle);
}

static void voice_recording_control_process_voice_activity(void)
{
    /* Latch timeout backstop: the quiet edge this latch waits for never
     * arrives when the AFE is starved (no VAD events at all).  Release it
     * after VOICE_RECORDING_CONTROL_AUTO_REARM_VAD_TIMEOUT_MS of total VAD
     * silence so one dead pipeline cannot end the wake rotation.
     * 2026-09-22 probe: with live VAD chatter the latch now holds at most
     * PROBE_FIRST_MS (+1 s backoff per consecutive probe, capped at
     * PROBE_MAX_MS) instead of waiting indefinitely for a quiet edge. */
    if (s_voice_auto_rearm_await_quiet_edge) {
        const bool starved =
            __atomic_load_n(&s_vad_event_total, __ATOMIC_RELAXED) ==
            s_voice_auto_rearm_latch_vad_events;
        uint32_t limit_ms = VOICE_RECORDING_CONTROL_AUTO_REARM_PROBE_FIRST_MS +
                            1000u * s_voice_auto_rearm_probe_count;
        if (starved) {
            limit_ms = VOICE_RECORDING_CONTROL_AUTO_REARM_VAD_TIMEOUT_MS;
        } else if (limit_ms >
                   VOICE_RECORDING_CONTROL_AUTO_REARM_PROBE_MAX_MS) {
            limit_ms = VOICE_RECORDING_CONTROL_AUTO_REARM_PROBE_MAX_MS;
        }
        TickType_t now_tick = xTaskGetTickCount();
        if ((int32_t)(now_tick - s_voice_auto_rearm_latch_set_tick) >=
            (int32_t)pdMS_TO_TICKS(limit_ms)) {
            if (starved) {
                voice_recording_control_clear_auto_rearm_latch("vad_timeout");
            } else {
                const uint32_t next_probe_count =
                    s_voice_auto_rearm_probe_count + 1u;
                voice_recording_control_clear_auto_rearm_latch("probe_due");
                /* clear() zeroed the ladder; restore the advanced count. */
                s_voice_auto_rearm_probe_count = next_probe_count;
            }
        }
    }
    voice_recording_control_vad_event_t event = {0};
    while (s_vad_queue != NULL &&
           xQueueReceive(s_vad_queue, &event, 0) == pdTRUE) {
        const bool auto_rearm_blocked =
            voice_recording_control_note_auto_rearm_vad_event(
                event.speech_detected);
        /* Hidden automatic candidates are wake windows, not full dictation.
         * If only voice_auto_start is on (auto_stop off), the VA machine never
         * silence/max-stops — ambient speech held the radio for tens of seconds
         * and owner could not re-arm 「开始录音」. Force auto-stop + short max
         * only while the candidate is still hidden; activate resets the machine
         * so visible dictation follows user auto_stop / full max_session. */
        const bool hidden_automatic =
            s_active_session_automatic && !s_active_session_visible &&
            s_state == VOICE_RECORDING_STATE_RECORDING;
        denzic_voice_activation_v1_config_t step_config =
            s_voice_activation_config;
        if (hidden_automatic) {
            /* Pre-roll plus a complete wake phrase. Ambient windows still
             * auto-stop; Type rejects clips that do not contain the phrase. */
            step_config.max_session_ms =
                VOICE_RECORDING_CONTROL_AUTOMATIC_WAKE_MAX_SESSION_MS;
            /* The host can still be receiving pre-roll and verifying the
             * phrase when VAD reports a one-second gap. Keep the complete
             * bounded wake window available for ACTIVATE; otherwise the
             * command arrives after the candidate has already become idle.
             * Visible/manual sessions retain their ordinary minimum. */
            step_config.min_session_ms = step_config.max_session_ms;
        }
        const bool visible_dictation =
            s_active_session_visible && !s_active_session_enrollment;
        /* An accepted wake transaction explicitly transfers endpoint ownership.
         * Manual visible sessions join the same host-owned endpoint only
         * after this session received host-recognized body speech. Before
         * that boundary, retain the bounded firmware VAD fallback so an
         * empty manual start cannot remain recording forever. */
        const bool host_endpoint_owned_visible =
            !s_active_session_enrollment && s_active_session_visible &&
            (s_active_session_host_endpoint_owned || s_active_session_body_speech_seen);
        step_config = voice_recording_endpoint_config(
            step_config, host_endpoint_owned_visible);
        /* Only Type's provider/owner timeline may establish body speech.
         * Firmware VAD remains useful for hidden wake candidates, but its
         * low-energy gaps are not authoritative enough to end visible text.
         * Accepted visible dictation uses Type's endpoint for both duration
         * and quiet-tail completion. The hidden wake window keeps its bound;
         * transport loss and protocol limits remain independent safeguards. */
        if (visible_dictation && !s_active_session_body_speech_seen) {
            step_config.silence_stop_ms =
                VOICE_RECORDING_CONTROL_INITIAL_BODY_WAIT_MS;
        }
        denzic_voice_activation_v1_input_t input = {
            .elapsed_ms = event.elapsed_ms,
            .enabled = s_voice_monitoring,
            .auto_start_enabled = s_voice_auto_start_enabled,
            .auto_stop_enabled =
                !s_active_session_enrollment &&
                (s_voice_auto_stop_enabled || hidden_automatic),
            .recording_active =
                s_state == VOICE_RECORDING_STATE_RECORDING,
            .speech_detected = event.speech_detected || host_endpoint_owned_visible,
            .start_blocked =
                s_state != VOICE_RECORDING_STATE_IDLE ||
                s_pending_start ||
                s_enrollment_start_pending ||
                s_cancel_pending ||
                auto_rearm_blocked,
        };
        denzic_voice_activation_v1_decision_t decision =
            denzic_voice_activation_v1_step(
                &s_voice_activation_machine,
                &step_config,
                &input);
        if (decision.action ==
                DENZIC_VOICE_ACTIVATION_V1_ACTION_START &&
            s_state == VOICE_RECORDING_STATE_IDLE &&
            !s_pending_start &&
            !s_enrollment_start_pending) {
            const bool transport_ready = ble_audio_stream_is_ready();
            esp_err_t start_ret = voice_recording_control_enter_recording(
                "voice_activation.auto_start",
                transport_ready, /* log only unexpected rejects */
                false,
                decision.pre_roll_ms);
            /* A short non-idle transport flap may race a VAD edge. Preserve
             * pre-roll while waiting for the ready notification. */
            if (start_ret == ESP_ERR_INVALID_STATE && !transport_ready) {
                voice_recording_control_schedule_pending_start(
                    "voice_activation.auto_start",
                    "audio_transport_not_ready",
                    "recording_waiting_for_ble_audio",
                    VOICE_RECORDING_PENDING_OWNER_AUTOMATIC_CANDIDATE);
            }
        } else if (
            decision.action ==
                DENZIC_VOICE_ACTIVATION_V1_ACTION_STOP &&
            s_state == VOICE_RECORDING_STATE_RECORDING) {
            const bool max_duration_stop =
                decision.stop_reason ==
                DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION;
            const bool silence_stop =
                decision.stop_reason ==
                DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_SILENCE;
            if (silence_stop &&
                voice_recording_control_host_speech_protection_active()) {
                continue;
            }
            ESP_LOGI(
                TAG,
                "voice auto-stop reason=%s recording_ms=%" PRIu32
                " silence_ms=%" PRIu32 " body_speech_seen=%u vad_queue_drops=%" PRIu32,
                decision.stop_reason ==
                        DENZIC_VOICE_ACTIVATION_V1_STOP_REASON_MAX_DURATION
                    ? "max_duration"
                    : "silence",
                s_voice_activation_machine.recording_ms,
                s_voice_activation_machine.silence_ms,
                s_active_session_body_speech_seen ? 1u : 0u,
                __atomic_load_n(
                    &s_vad_queue_drop_count,
                    __ATOMIC_RELAXED));
            esp_err_t stop_ret = voice_recording_control_exit_recording_with_origin(
                max_duration_stop
                    ? "voice_activation.auto_stop_max_duration"
                    : "voice_activation.auto_stop_silence",
                max_duration_stop
                    ? AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION_MAX_DURATION
                    : AUDIO_CAPTURE_STOP_ORIGIN_VOICE_ACTIVATION);
            if (hidden_automatic && max_duration_stop) {
                if (stop_ret != ESP_OK) {
                    voice_recording_control_set_auto_rearm_latch(
                        "max_duration_stop_failed");
                }
                /* A max-duration stop is a streaming window rotation, not the
                 * end of an utterance. A successful stop must not latch the
                 * entire VAD episode: its 2-5s hold plus speech confirmation
                 * exceeds the 2s pre-roll and loses a crossing wake phrase.
                 * The transfer-state/transport gates still prevent overlapping
                 * physical sessions. Type still requires a complete phrase
                 * and independently qualified owner evidence. */
                denzic_voice_activation_v1_reset(
                    &s_voice_activation_machine);
            }
        }
    }
}

static void voice_recording_control_task(void *parameter)
{
    (void)parameter;
    voice_stack_diag_bind_current_task();
    (void)watchdog_platform_subscribe_current_task("voice_recording_control_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (__atomic_exchange_n(
                &s_power_state_refresh_pending,
                false,
                __ATOMIC_ACQ_REL)) {
            voice_recording_control_apply_power_state_change();
            watchdog_platform_feed_current_task();
        }

        voice_recording_control_command_t queued = {0};
        for (uint32_t command_index = 0;
             command_index < VOICE_RECORDING_CONTROL_COMMANDS_PER_TICK &&
             s_command_queue != NULL &&
             xQueueReceive(s_command_queue, &queued, 0) == pdTRUE;
             ++command_index) {
            esp_err_t command_ret = voice_recording_control_handle_control_command(
                queued.command,
                queued.source);
            if (strcmp(queued.source, "usb") == 0 &&
                strcmp(queued.command, "VREC:RECOVERY:TYPE:SILENT:FRESH") == 0) {
                printf(
                    "~VREC:RESULT command=%s result=%s\n",
                    queued.command,
                    command_ret == ESP_OK ? "OK" : esp_err_to_name(command_ret));
            }
            watchdog_platform_feed_current_task();
        }

        voice_recording_control_refresh_voice_monitoring();
        voice_recording_control_poll_ensure_reservation();
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
            (void)voice_recording_control_recovery(
                source != NULL ? source : "voice_key_hold",
                false,
                false,
                false,
                false);
        }

        if ((s_state == VOICE_RECORDING_STATE_RECORDING || s_state == VOICE_RECORDING_STATE_TRANSFERRING) &&
            !audio_capture_session_is_active()) {
            voice_recording_control_handle_session_inactive();
        }

        voice_recording_control_try_start_pending_enrollment("task_poll");
        voice_recording_control_poll_pending_start();
        (void)watchdog_platform_task_notify_take(
            pdTRUE,
            VOICE_RECORDING_CONTROL_SESSION_CHECK_MS);
    }
}

uint32_t voice_recording_control_get_session_count(void)
{
    return __atomic_load_n(&s_session_count, __ATOMIC_RELAXED);
}

static esp_err_t voice_recording_control_start_internal(bool enable_audio_capture)
{
    if (s_started) {
        return ESP_OK;
    }
    if (s_vad_queue == NULL) {
        s_vad_queue = xQueueCreate(
            VOICE_RECORDING_CONTROL_VAD_QUEUE_LENGTH,
            sizeof(voice_recording_control_vad_event_t));
        if (s_vad_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_command_queue == NULL) {
        s_command_queue = xQueueCreate(
            VOICE_RECORDING_CONTROL_COMMAND_QUEUE_LENGTH,
            sizeof(voice_recording_control_command_t));
        if (s_command_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_voice_activation_config =
        denzic_voice_activation_v1_default_config();
    s_voice_activation_config.silence_stop_ms =
        VOICE_RECORDING_CONTROL_DICTATION_SILENCE_STOP_MS;
    s_voice_activation_config.tail_ms =
        VOICE_RECORDING_CONTROL_DICTATION_TAIL_MS;
    /* Product VA window for 「开始录音」(~0.8–1.0 s):
     * - speech_confirm 420: fewer ambient ticks than 300 (owner: 假窗刷屏导致
     *   低功耗进不去 + 真词被占窗). Still well under 1.0.3-feel latency.
     * - pre_roll 2000: preserve a softly spoken wake phrase even when VAD does
     *   not cross its start threshold until the louder dictation body begins.
     *   Type still requires KWS/local-ASR phrase evidence, so the extra history
     *   does not turn arbitrary owner speech into a wake.
     * - cooldown 400: re-arm after a miss without flooding Type (zero cooldown
     *   opened overlapping windows, websocket stalled, capsule stayed empty).
     * - max_session for hidden path is forced to 4000 in process_voice_activity.
     */
    s_voice_activation_config.speech_confirm_ms = 420u;
    s_voice_activation_config.pre_roll_ms = 2000u;
    s_voice_activation_config.cooldown_ms = 400u;
    voice_recording_control_clear_auto_rearm_latch(
        "voice_recording_control_start");
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
    ble_audio_stream_set_control_write_handler(
        voice_recording_control_ble_control_write);
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
