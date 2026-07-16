#include "diag_log.h"
#include "diag_log_platform.h"
#include "denzic_observability_v1_generated.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "diag_log";

static bool s_dumping;
static portMUX_TYPE s_dumping_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_source_mask_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint16_t source;
    const char *name;
    bool info_default_enabled;
} diag_log_source_def_t;

static const diag_log_source_def_t s_source_defs[] = {
    {DIAG_SRC_SYSTEM, "system", true},
    {DIAG_SRC_KEYBOARD, "keyboard", false},
    {DIAG_SRC_BLE_HID, "ble_hid", true},
    {DIAG_SRC_BLE_GAP, "ble_gap", true},
    {DIAG_SRC_AUDIO, "audio", false},
    {DIAG_SRC_VOICE_REC, "voice_rec", false},
    {DIAG_SRC_VOICE_KEY, "voice_key", false},
    {DIAG_SRC_SELF_TEST, "self_test", true},
    {DIAG_SRC_HEALTH, "health", true},
    {DIAG_SRC_BLE_AUDIO, "ble_audio", false},
    {DIAG_SRC_OTA, "ota", true},
    {DIAG_SRC_POWER, "power", true},
    {DIAG_SRC_BOARD, "board", true},
    {DIAG_SRC_STATUS_LED, "status_led", true},
};

static uint32_t s_source_mask;

static uint32_t diag_log_source_bit(uint16_t source)
{
    if (source == 0 || source > 31) {
        return 0;
    }
    return 1u << source;
}

static const diag_log_source_def_t *diag_log_find_source(uint16_t source)
{
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (s_source_defs[i].source == source) {
            return &s_source_defs[i];
        }
    }
    return NULL;
}

static uint32_t diag_log_default_source_mask(void)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (s_source_defs[i].info_default_enabled) {
            mask |= diag_log_source_bit(s_source_defs[i].source);
        }
    }
    return mask;
}

static bool diag_log_source_allowed(uint16_t source, uint8_t severity)
{
    if (severity >= DIAG_SEV_WARN) {
        return true;
    }

    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    bool enabled = (s_source_mask & bit) != 0;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return enabled;
}

#define DIAG_USB_CMD_PREFIX "DIAGLOG:"
#define DIAG_USB_CMD_MAX 64
#define DIAG_PLATFORM_EXPORT_MAX_EVENTS 128U
#define DIAG_PLATFORM_CORRELATION_PREFIX UINT64_C(0x4c53544e00000000)

typedef struct {
    uint64_t correlation_id;
    uint32_t next_sequence;
} diag_platform_sequence_entry_t;

typedef struct {
    uint64_t ble_correlation_id;
    uint32_t ble_recovery_started_at_ms;
    uint64_t ota_correlation_id;
    uint32_t ota_started_at_ms;
} diag_platform_export_context_t;

static const char *diag_platform_ble_state_name(denzic_observability_v1_ble_lifecycle_state_t value)
{
    switch (value) {
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_DISCONNECTED:
        return "disconnected";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_ADVERTISING:
        return "advertising";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_CONNECTING:
        return "connecting";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_CONNECTED_IDLE:
        return "connected_idle";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_RECORDING:
        return "recording";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_RECOVERING:
        return "recovering";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_PAIRED_ELSEWHERE:
        return "paired_elsewhere";
    case DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

static const char *diag_platform_command_result_name(denzic_observability_v1_command_result_t value)
{
    switch (value) {
    case DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED:
        return "succeeded";
    case DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_CANCELLED:
        return "cancelled";
    case DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED:
        return "failed";
    case DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_TIMEOUT:
        return "timeout";
    case DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_STARTED:
    default:
        return "started";
    }
}

static const char *diag_platform_error_category_name(denzic_observability_v1_error_category_t value)
{
    switch (value) {
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_DEVICE:
        return "device";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_TRANSPORT:
        return "transport";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_HOST:
        return "host";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_PROVIDER:
        return "provider";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_NETWORK:
        return "network";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_PROTOCOL:
        return "protocol";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_RESOURCE:
        return "resource";
    case DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_NONE:
    default:
        return "none";
    }
}

static const char *diag_platform_timing_metric_name(denzic_observability_v1_timing_metric_t value)
{
    switch (value) {
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_EDGE_TO_RECORD_DISPATCH_MS:
        return "edge_to_record_dispatch_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_BLE_RECOVERY_MS:
        return "ble_recovery_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_AUDIO_FIRST_PACKET_MS:
        return "audio_first_packet_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_PREVIEW_LATENCY_MS:
        return "preview_latency_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_FINAL_TRANSCRIPTION_MS:
        return "final_transcription_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_OTA_TRANSFER_MS:
        return "ota_transfer_ms";
    case DENZIC_OBSERVABILITY_V1_TIMING_METRIC_NONE:
    default:
        return "none";
    }
}

static const char *diag_platform_severity_name(uint8_t severity)
{
    if (severity >= DIAG_SEV_ERROR) {
        return "error";
    }
    return severity == DIAG_SEV_WARN ? "warn" : "info";
}

static const char *diag_platform_capability_name(denzic_observability_v1_capability_t capability)
{
    switch (capability) {
    case DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE:
        return "ble";
    case DENZIC_OBSERVABILITY_V1_CAPABILITY_OTA:
        return "ota";
    case DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO:
    default:
        return "audio";
    }
}

static uint64_t diag_platform_correlation_id(
    denzic_observability_v1_capability_t capability,
    uint32_t operation_key)
{
    uint32_t normalized_key = operation_key == 0 ? 1U : operation_key;
    return DIAG_PLATFORM_CORRELATION_PREFIX |
           ((uint64_t)capability << 28) |
           (uint64_t)(normalized_key & 0x0fffffffU);
}

static bool diag_platform_capability_for_event(
    const diag_log_event_wire_t *raw,
    denzic_observability_v1_capability_t *out_capability)
{
    if (raw == NULL || out_capability == NULL) {
        return false;
    }

    switch (raw->source) {
    case DIAG_SRC_BLE_HID:
    case DIAG_SRC_BLE_GAP:
        *out_capability = DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE;
        return true;
    case DIAG_SRC_AUDIO:
    case DIAG_SRC_VOICE_REC:
    case DIAG_SRC_VOICE_KEY:
    case DIAG_SRC_BLE_AUDIO:
        *out_capability = DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO;
        return true;
    case DIAG_SRC_KEYBOARD:
        if (raw->event == DIAG_KBD_INPUT_DEBUG) {
            *out_capability = DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO;
            return true;
        }
        return false;
    case DIAG_SRC_OTA:
        *out_capability = DENZIC_OBSERVABILITY_V1_CAPABILITY_OTA;
        return true;
    default:
        return false;
    }
}

static uint32_t diag_platform_audio_session_id(const diag_log_event_wire_t *raw)
{
    if (raw->source == DIAG_SRC_VOICE_REC) {
        return raw->arg3;
    }
    if (raw->source == DIAG_SRC_AUDIO) {
        if (raw->event == DIAG_AUDIO_SESSION) {
            return raw->arg2;
        }
        if (raw->event == DIAG_AUDIO_BACKPRESSURE) {
            return raw->arg1;
        }
    }
    if (raw->source == DIAG_SRC_BLE_AUDIO) {
        return raw->event == DIAG_BAUD_STATE_CHANGE ? raw->arg4 : raw->arg1;
    }
    return 0;
}

static uint64_t diag_platform_assign_correlation(
    const diag_log_event_wire_t *raw,
    denzic_observability_v1_capability_t capability,
    diag_platform_export_context_t *context)
{
    if (capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO) {
        uint32_t session_id = diag_platform_audio_session_id(raw);
        return diag_platform_correlation_id(
            capability,
            session_id != 0 ? session_id : raw->timestamp_ms);
    }

    if (capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE) {
        if (raw->source == DIAG_SRC_BLE_HID && raw->event == DIAG_BLE_CONNECT) {
            context->ble_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
            context->ble_recovery_started_at_ms = 0;
        } else if (raw->source == DIAG_SRC_BLE_HID && raw->event == DIAG_BLE_DISCONNECT) {
            if (context->ble_correlation_id == 0) {
                context->ble_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
            }
        } else if (raw->source == DIAG_SRC_BLE_GAP && raw->event == DIAG_GAP_RECOVERY &&
                   (context->ble_correlation_id == 0 || raw->arg1 == 1U)) {
            context->ble_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
            context->ble_recovery_started_at_ms = raw->timestamp_ms;
        }
        if (context->ble_correlation_id == 0) {
            context->ble_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
        }
        return context->ble_correlation_id;
    }

    if (raw->event == DIAG_OTA_BEGIN && raw->arg3 == 0) {
        context->ota_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
        context->ota_started_at_ms = raw->timestamp_ms;
    }
    if (context->ota_correlation_id == 0) {
        context->ota_correlation_id = diag_platform_correlation_id(capability, raw->timestamp_ms);
        context->ota_started_at_ms = raw->timestamp_ms;
    }
    return context->ota_correlation_id;
}

static uint32_t diag_platform_next_sequence(
    diag_platform_sequence_entry_t *entries,
    uint32_t *entry_count,
    uint32_t capacity,
    uint64_t correlation_id)
{
    for (uint32_t index = 0; index < *entry_count; ++index) {
        if (entries[index].correlation_id == correlation_id) {
            entries[index].next_sequence++;
            return entries[index].next_sequence;
        }
    }

    if (*entry_count >= capacity) {
        return 1U;
    }
    entries[*entry_count].correlation_id = correlation_id;
    entries[*entry_count].next_sequence = 1U;
    (*entry_count)++;
    return 1U;
}

static void diag_platform_apply_result_and_state(
    const diag_log_event_wire_t *raw,
    denzic_observability_v1_event_t *event,
    diag_platform_export_context_t *context)
{
    if (raw->severity >= DIAG_SEV_WARN) {
        event->command_result = DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
        if (event->capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE ||
            raw->source == DIAG_SRC_BLE_AUDIO ||
            (raw->source == DIAG_SRC_AUDIO && raw->event == DIAG_AUDIO_BACKPRESSURE)) {
            event->error_category = DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_TRANSPORT;
        } else if (event->capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_OTA &&
                   raw->event == DIAG_OTA_REJECTED) {
            event->error_category = DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_RESOURCE;
        } else {
            event->error_category = DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_DEVICE;
        }
    }

    if (event->capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_BLE) {
        if (raw->source == DIAG_SRC_BLE_HID && raw->event == DIAG_BLE_CONNECT) {
            event->ble_lifecycle_state = raw->arg1 != 0
                ? DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_CONNECTED_IDLE
                : DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_DISCONNECTED;
            event->command_result = raw->arg1 != 0
                ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED
                : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
        } else if (raw->source == DIAG_SRC_BLE_HID && raw->event == DIAG_BLE_DISCONNECT) {
            event->ble_lifecycle_state = DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_DISCONNECTED;
            event->command_result = DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED;
            event->error_category = DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_NONE;
        } else if (raw->source == DIAG_SRC_BLE_GAP && raw->event == DIAG_GAP_ADV_START) {
            event->ble_lifecycle_state = DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_ADVERTISING;
            event->command_result = raw->arg1 != 0
                ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED
                : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
        } else if (raw->source == DIAG_SRC_BLE_GAP && raw->event == DIAG_GAP_RECOVERY) {
            event->ble_lifecycle_state = DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_RECOVERING;
            if (raw->arg1 == 4U && raw->arg2 == 0 && context->ble_recovery_started_at_ms != 0) {
                event->command_result = DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED;
                event->timing_metric = DENZIC_OBSERVABILITY_V1_TIMING_METRIC_BLE_RECOVERY_MS;
                event->timing_value_ms = raw->timestamp_ms - context->ble_recovery_started_at_ms;
                context->ble_recovery_started_at_ms = 0;
            } else if (raw->arg2 == 0) {
                event->command_result = DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_STARTED;
                event->error_category = DENZIC_OBSERVABILITY_V1_ERROR_CATEGORY_NONE;
            }
        }
        return;
    }

    if (event->capability == DENZIC_OBSERVABILITY_V1_CAPABILITY_AUDIO) {
        if (raw->source == DIAG_SRC_VOICE_REC && raw->event == DIAG_VREC_TIMING) {
            event->ble_lifecycle_state = DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_RECORDING;
            event->command_result = DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED;
            event->timing_metric = DENZIC_OBSERVABILITY_V1_TIMING_METRIC_EDGE_TO_RECORD_DISPATCH_MS;
            event->timing_value_ms = raw->arg1;
        } else if ((raw->source == DIAG_SRC_VOICE_REC && raw->event == DIAG_VREC_SESSION) ||
                   (raw->source == DIAG_SRC_AUDIO && raw->event == DIAG_AUDIO_SESSION)) {
            uint32_t session_action = raw->arg1;
            event->ble_lifecycle_state = session_action == 1U
                ? DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_RECORDING
                : DENZIC_OBSERVABILITY_V1_BLE_LIFECYCLE_STATE_CONNECTED_IDLE;
            event->command_result = session_action == 3U
                ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_CANCELLED
                : (session_action == 2U
                    ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED
                    : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_STARTED);
        }
        return;
    }

    if (raw->event == DIAG_OTA_BEGIN) {
        event->command_result = raw->arg3 == 0
            ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_STARTED
            : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
    } else if (raw->event == DIAG_OTA_ABORT) {
        event->command_result = raw->arg3 == 0
            ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_CANCELLED
            : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
        context->ota_correlation_id = 0;
        context->ota_started_at_ms = 0;
    } else if (raw->event == DIAG_OTA_VERIFY || raw->event == DIAG_OTA_SET_BOOT) {
        event->command_result = raw->arg3 == 0
            ? DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_SUCCEEDED
            : DENZIC_OBSERVABILITY_V1_COMMAND_RESULT_FAILED;
        if (raw->event == DIAG_OTA_SET_BOOT && raw->arg3 == 0 && context->ota_started_at_ms != 0) {
            event->timing_metric = DENZIC_OBSERVABILITY_V1_TIMING_METRIC_OTA_TRANSFER_MS;
            event->timing_value_ms = raw->timestamp_ms - context->ota_started_at_ms;
            context->ota_correlation_id = 0;
            context->ota_started_at_ms = 0;
        }
    }
}

static bool diag_platform_map_event(
    const diag_log_event_wire_t *raw,
    diag_platform_export_context_t *context,
    denzic_observability_v1_event_t *out_event)
{
    denzic_observability_v1_capability_t capability;
    if (!diag_platform_capability_for_event(raw, &capability)) {
        return false;
    }

    uint64_t correlation_id = diag_platform_assign_correlation(raw, capability, context);
    denzic_observability_v1_event_init(
        out_event,
        correlation_id,
        0,
        raw->timestamp_ms,
        DENZIC_OBSERVABILITY_V1_EVENT_SOURCE_FIRMWARE,
        capability);
    diag_platform_apply_result_and_state(raw, out_event, context);
    return true;
}

static void diag_platform_print_event(
    const diag_log_event_wire_t *raw,
    const denzic_observability_v1_event_t *event)
{
    printf(
        "{\"contract\":\"%s\",\"contract_version\":%u,\"correlation_id\":%" PRIu64
        ",\"event_sequence\":%" PRIu32 ",\"monotonic_ms\":%" PRIu32
        ",\"source\":\"firmware\",\"capability\":\"%s\",\"ble_lifecycle_state\":\"%s\""
        ",\"command_result\":\"%s\",\"error_category\":\"%s\",\"timing_metric\":\"%s\""
        ",\"timing_value_ms\":%" PRIu32
        ",\"diagnostic\":{\"source\":\"%s\",\"event\":%u,\"severity\":\"%s\""
        ",\"arg1\":%" PRIu32 ",\"arg2\":%" PRIu32 ",\"arg3\":%" PRIu32 ",\"arg4\":%" PRIu32 "}}\n",
        DENZIC_OBSERVABILITY_V1_CONTRACT_NAME,
        (unsigned)event->contract_version,
        event->correlation_id,
        event->event_sequence,
        event->monotonic_ms,
        diag_platform_capability_name(event->capability),
        diag_platform_ble_state_name(event->ble_lifecycle_state),
        diag_platform_command_result_name(event->command_result),
        diag_platform_error_category_name(event->error_category),
        diag_platform_timing_metric_name(event->timing_metric),
        event->timing_value_ms,
        diag_log_source_name(raw->source),
        (unsigned)raw->event,
        diag_platform_severity_name(raw->severity),
        raw->arg1,
        raw->arg2,
        raw->arg3,
        raw->arg4);
}

static void diag_log_set_dumping(bool dumping)
{
    portENTER_CRITICAL(&s_dumping_lock);
    s_dumping = dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
}

static bool diag_log_get_dumping(void)
{
    portENTER_CRITICAL(&s_dumping_lock);
    bool dumping = s_dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
    return dumping;
}

void diag_log_init(void)
{
    portENTER_CRITICAL(&s_source_mask_lock);
    s_source_mask = diag_log_default_source_mask();
    portEXIT_CRITICAL(&s_source_mask_lock);

    diag_log_platform_init();
    diag_log_write(DIAG_SRC_SYSTEM, DIAG_SYS_INIT_RESULT, DIAG_SEV_INFO,
                   DIAG_COMP_DIAG_LOG, 0, 0, 0);
}

void diag_log_write(uint16_t source, uint8_t event, uint8_t severity,
                    uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    if (!diag_log_source_allowed(source, severity)) {
        return;
    }
    diag_log_platform_write(source, event, severity, arg1, arg2, arg3, arg4);
}

uint32_t diag_log_source_mask(void)
{
    portENTER_CRITICAL(&s_source_mask_lock);
    uint32_t mask = s_source_mask;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return mask;
}

bool diag_log_source_is_enabled(uint16_t source)
{
    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    bool enabled = (s_source_mask & bit) != 0;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return enabled;
}

bool diag_log_source_set_enabled(uint16_t source, bool enabled)
{
    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0 || diag_log_find_source(source) == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    if (enabled) {
        s_source_mask |= bit;
    } else {
        s_source_mask &= ~bit;
    }
    portEXIT_CRITICAL(&s_source_mask_lock);
    return true;
}

const char *diag_log_source_name(uint16_t source)
{
    const diag_log_source_def_t *def = diag_log_find_source(source);
    return def != NULL ? def->name : "unknown";
}

bool diag_log_source_from_name(const char *name, uint16_t *out_source)
{
    if (name == NULL || out_source == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (strcmp(name, s_source_defs[i].name) == 0) {
            *out_source = s_source_defs[i].source;
            return true;
        }
    }
    return false;
}

uint32_t diag_log_count(void)
{
    return diag_log_platform_count();
}

void diag_log_dump(void)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump();
    diag_log_set_dumping(false);
}

void diag_log_dump_last(uint32_t count)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump_last(count);
    diag_log_set_dumping(false);
}

void diag_log_dump_last_by_source(uint32_t count, uint16_t source)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump_last_by_source(count, source);
    diag_log_set_dumping(false);
}

void diag_log_clear(void)
{
    diag_log_platform_clear();
}

bool diag_log_is_dumping(void)
{
    return diag_log_get_dumping() || diag_log_platform_is_dumping();
}

uint32_t diag_log_read_range(uint32_t offset, uint32_t limit,
                              void *buffer, uint32_t buffer_size)
{
    return diag_log_platform_read_range(offset, limit, buffer, buffer_size);
}

void diag_log_dump_platform_last(uint32_t count)
{
    if (count == 0 || count > DIAG_PLATFORM_EXPORT_MAX_EVENTS) {
        ESP_LOGW(
            TAG,
            "DIAGLOG PLATFORM: count must be 1..%u, got %" PRIu32,
            DIAG_PLATFORM_EXPORT_MAX_EVENTS,
            count);
        return;
    }

    uint32_t retained = diag_log_count();
    uint32_t offset = retained > count ? retained - count : 0;
    diag_log_event_wire_t *events = calloc(count, sizeof(*events));
    diag_platform_sequence_entry_t *sequences = calloc(count, sizeof(*sequences));
    if (events == NULL || sequences == NULL) {
        free(events);
        free(sequences);
        ESP_LOGW(TAG, "DIAGLOG PLATFORM: snapshot allocation failed for %" PRIu32 " events", count);
        return;
    }

    diag_log_set_dumping(true);
    uint32_t read = diag_log_read_range(offset, count, events, count * sizeof(*events));
    diag_platform_export_context_t context = {0};
    uint32_t sequence_count = 0;
    uint32_t exported = 0;
    printf(
        "~DIAGLOG:PLATFORM contract=%s version=%u requested=%" PRIu32
        " retained=%" PRIu32 " read=%" PRIu32 "\n",
        DENZIC_OBSERVABILITY_V1_CONTRACT_NAME,
        (unsigned)DENZIC_OBSERVABILITY_V1_CONTRACT_VERSION,
        count,
        retained,
        read);
    for (uint32_t index = 0; index < read; ++index) {
        denzic_observability_v1_event_t event;
        if (!diag_platform_map_event(&events[index], &context, &event)) {
            continue;
        }
        event.event_sequence = diag_platform_next_sequence(
            sequences,
            &sequence_count,
            count,
            event.correlation_id);
        diag_platform_print_event(&events[index], &event);
        exported++;
    }
    fflush(stdout);
    diag_log_set_dumping(false);
    free(sequences);
    free(events);
    ESP_LOGI(
        TAG,
        "DIAGLOG PLATFORM: exported %" PRIu32 " of %" PRIu32 " retained events",
        exported,
        read);
}

static void diag_log_print_sources(void)
{
    uint32_t mask = diag_log_source_mask();
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        const diag_log_source_def_t *def = &s_source_defs[i];
        uint32_t bit = diag_log_source_bit(def->source);
        printf(
            "~DIAGLOG:SOURCE name=%s id=0x%02x info_enabled=%u info_default=%u warn_error_always=1 mask=0x%08" PRIx32 "\n",
            def->name,
            (unsigned)def->source,
            (mask & bit) != 0 ? 1u : 0u,
            def->info_default_enabled ? 1u : 0u,
            mask);
    }
    fflush(stdout);
}

static const char *diag_log_skip_separator(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    while (*text == ':' || *text == ' ') {
        text++;
    }
    return text;
}

static bool diag_log_parse_source_token(const char *text, uint16_t *out_source)
{
    if (text == NULL || out_source == NULL || *text == '\0') {
        return false;
    }

    char token[24] = {0};
    size_t len = 0;
    while (text[len] != '\0' && text[len] != '\r' && text[len] != '\n' &&
           text[len] != ':' && text[len] != ' ') {
        if (len + 1 >= sizeof(token)) {
            return false;
        }
        char c = text[len];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        token[len] = c;
        len++;
    }

    if (len == 0) {
        return false;
    }

    return diag_log_source_from_name(token, out_source);
}

bool diag_log_consume_usb_command(const char *line)
{
    if (line == NULL) {
        return false;
    }

    if (*line == '~') {
        line++;
    }

    size_t prefix_len = strlen(DIAG_USB_CMD_PREFIX);
    if (strncmp(line, DIAG_USB_CMD_PREFIX, prefix_len) != 0) {
        return false;
    }

    char cmd_buffer[DIAG_USB_CMD_MAX] = {0};
    const char *cmd_start = line + prefix_len;
    size_t cmd_len = 0;
    while (cmd_start[cmd_len] != '\0' && cmd_start[cmd_len] != '\r' && cmd_start[cmd_len] != '\n') {
        if (cmd_len + 1 >= sizeof(cmd_buffer)) {
            ESP_LOGW(TAG, "DIAGLOG: command too long");
            return true;
        }
        cmd_buffer[cmd_len] = cmd_start[cmd_len];
        cmd_len++;
    }

    if (strcmp(cmd_buffer, "DUMP") == 0) {
        diag_log_dump();
        return true;
    }
    if (strcmp(cmd_buffer, "COUNT") == 0) {
        ESP_LOGI(TAG, "DIAGLOG COUNT: %" PRIu32, diag_log_count());
        return true;
    }
    if (strcmp(cmd_buffer, "CLEAR") == 0) {
        diag_log_clear();
        return true;
    }
    if (strcmp(cmd_buffer, "INPUTDBG") == 0 || strcmp(cmd_buffer, "INPUTDBG:STATUS") == 0) {
        ESP_LOGI(TAG, "DIAGLOG INPUTDBG: %s", diag_log_input_debug_enabled() ? "ON" : "OFF");
        return true;
    }
    if (strcmp(cmd_buffer, "INPUTDBG:ON") == 0) {
        diag_log_set_input_debug_enabled(true);
        return true;
    }
    if (strcmp(cmd_buffer, "INPUTDBG:OFF") == 0) {
        diag_log_set_input_debug_enabled(false);
        return true;
    }
    if (strcmp(cmd_buffer, "SOURCES") == 0) {
        diag_log_print_sources();
        return true;
    }
    if (strncmp(cmd_buffer, "ENABLE", 6) == 0) {
        uint16_t source = 0;
        if (diag_log_parse_source_token(diag_log_skip_separator(cmd_buffer + 6), &source) &&
            diag_log_source_set_enabled(source, true)) {
            ESP_LOGI(TAG, "DIAGLOG SOURCE: %s enabled", diag_log_source_name(source));
            diag_log_print_sources();
            return true;
        }
        ESP_LOGW(TAG, "DIAGLOG: unknown source for ENABLE: %s", cmd_buffer + 6);
        return true;
    }
    if (strncmp(cmd_buffer, "DISABLE", 7) == 0) {
        uint16_t source = 0;
        if (diag_log_parse_source_token(diag_log_skip_separator(cmd_buffer + 7), &source) &&
            diag_log_source_set_enabled(source, false)) {
            ESP_LOGI(TAG, "DIAGLOG SOURCE: %s disabled", diag_log_source_name(source));
            diag_log_print_sources();
            return true;
        }
        ESP_LOGW(TAG, "DIAGLOG: unknown source for DISABLE: %s", cmd_buffer + 7);
        return true;
    }
    if (strncmp(cmd_buffer, "LAST:", 5) == 0) {
        char *tail = cmd_buffer + 5;
        uint32_t n = (uint32_t)atoi(tail);
        if (n > 0) {
            char *source_sep = strchr(tail, ':');
            if (source_sep != NULL) {
                uint16_t source = 0;
                if (diag_log_parse_source_token(source_sep + 1, &source)) {
                    diag_log_dump_last_by_source(n, source);
                } else {
                    ESP_LOGW(TAG, "DIAGLOG: unknown source for LAST: %s", source_sep + 1);
                }
            } else {
                diag_log_dump_last(n);
            }
        }
        return true;
    }
    if (strncmp(cmd_buffer, "PLATFORM:LAST:", strlen("PLATFORM:LAST:")) == 0) {
        uint32_t n = (uint32_t)atoi(cmd_buffer + strlen("PLATFORM:LAST:"));
        diag_log_dump_platform_last(n);
        return true;
    }

    ESP_LOGW(TAG, "DIAGLOG: unknown command: %s", cmd_buffer);
    return true;
}
