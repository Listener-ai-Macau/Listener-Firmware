#include "ble_audio_stream.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include "diag_log.h"
#include "listener_device.h"
#include "listener_audio_proto.h"
#include "watchdog_platform.h"

#define BLE_AUDIO_STREAM_TASK_STACK_BYTES (5 * 1024)
#define BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES 244
#define BLE_AUDIO_STREAM_PACKET_MAX_BYTES 500
#ifdef CONFIG_SPIRAM
#define BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH 256
#define BLE_AUDIO_STREAM_AUDIO_POOL_EXTRA 8
#else
#define BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH 48
#define BLE_AUDIO_STREAM_AUDIO_POOL_EXTRA 4
#endif
#define BLE_AUDIO_STREAM_NOTIFY_WINDOW_DEPTH 3
#define BLE_AUDIO_STREAM_NOTIFY_WAIT_MS 1000
#define BLE_AUDIO_STREAM_NOTIFY_SUCCESS_DELAY_MS 20
#define BLE_AUDIO_STREAM_NOTIFY_BACKLOG_SUCCESS_DELAY_MS 2
#define BLE_AUDIO_STREAM_NOTIFY_BACKLOG_QUEUE_THRESHOLD (BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH / 2)
#define BLE_AUDIO_STREAM_NOTIFY_RETRY_DELAY_MS 20
#define BLE_AUDIO_STREAM_NOTIFY_TX_DONE_WAIT_MS 1000
#define BLE_AUDIO_STREAM_NOTIFY_RETRY_LIMIT 80
#define BLE_AUDIO_STREAM_NOTIFY_RETRY_LOG_INTERVAL 40
#define BLE_AUDIO_NOTIFY_STATE_DISABLED 0U
#define BLE_AUDIO_NOTIFY_STATE_ENABLED 1U
#define BLE_AUDIO_NOTIFY_STATE_DISABLED_ABORT 2U
#define BLE_AUDIO_NOTIFY_STATE_DEFERRED_DISABLED 3U
#define BLE_AUDIO_NOTIFY_STATE_DEFERRED_ENABLED 4U
#define BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS 20000
#define BLE_AUDIO_STREAM_LINK_RECOVERY_POLL_MS 50
#define BLE_AUDIO_STREAM_LINK_RECOVERY_RESUME_DELAY_MS 500
#define BLE_AUDIO_STREAM_AUDIO_QUEUE_WAIT_MS 100
#define BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES 1920
#define BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH (BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH + BLE_AUDIO_STREAM_AUDIO_POOL_EXTRA)
#define BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS 48
#define BLE_AUDIO_STREAM_REPLAY_PAYLOAD_BYTES (BLE_AUDIO_STREAM_PACKET_MAX_BYTES - LISTENER_AUDIO_PROTO_HEADER_BYTES)
#define BLE_AUDIO_STREAM_AUDIO_POOL_PRESSURE_WARN_PERCENT 80U
#define BLE_AUDIO_STREAM_AUDIO_POOL_PRESSURE_WARN_LEVEL \
    ((BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH * BLE_AUDIO_STREAM_AUDIO_POOL_PRESSURE_WARN_PERCENT + 99U) / 100U)
#define BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT 95U
#define BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT 70U
#define BLE_AUDIO_STREAM_CONTROL_NOTIFY_REPETITIONS 3
#define BLE_AUDIO_STREAM_CONTROL_NOTIFY_REPEAT_DELAY_MS 5
#define BLE_AUDIO_STREAM_TASK_QUEUE_WAIT_MS 1000
#define BLE_AUDIO_STREAM_CONTROL_MAX_BYTES 64

typedef enum {
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START = 0,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_FINALIZE_STOP,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_ERROR,
} ble_audio_stream_job_type_t;

typedef enum {
    BLE_AUDIO_STREAM_RETRY_CAUSE_MBUF_ALLOC = 0,
    BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_ENOMEM,
    BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_TIMEOUT,
    BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_STATUS,
    BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_OTHER,
} ble_audio_stream_retry_cause_t;

typedef enum {
    BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED = 0,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_MTU_READY,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED,
    BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR,
} ble_audio_stream_transport_state_t;

typedef struct {
    ble_audio_stream_job_type_t type;
    uint32_t session_id;
    uint16_t packet_sequence;
    uint16_t pcm_bytes;
    uint16_t expected_packet_count;
    uint16_t error_code;
    uint8_t *owned_pcm;
} ble_audio_stream_job_t;

typedef struct {
    bool active;
    uint32_t session_id;
    uint32_t notify_sent;
    uint32_t notify_failed;
    uint32_t notify_retries;
    uint32_t notify_mbuf_alloc_retries;
    uint32_t notify_enomem_retries;
    uint32_t notify_tx_timeout_retries;
    uint32_t notify_tx_status_retries;
    uint32_t notify_other_retries;
    uint32_t audio_packets_sent;
    uint32_t audio_packets_failed;
    uint32_t queue_jobs_purged;
    uint32_t audio_pool_high_water;
    uint32_t audio_pool_alloc_failed;
    uint32_t audio_queue_full;
    bool audio_pool_pressure_warned;
    const char *last_drop_reason;
    int last_error;
} ble_audio_stream_session_stats_t;

typedef struct {
    uint32_t subscribe;
    uint32_t mtu;
    uint32_t notify_tx;
    uint32_t disconnect;
} ble_audio_stream_stale_event_counts_t;

typedef struct {
    bool valid;
    uint32_t session_id;
    uint16_t sequence;
    uint16_t payload_len;
    uint16_t packet_pcm_bytes;
    uint8_t payload[BLE_AUDIO_STREAM_REPLAY_PAYLOAD_BYTES];
} ble_audio_stream_replay_packet_t;

static const char *TAG = "ble_audio_stream";

static const ble_uuid128_t s_service_uuid = BLE_AUDIO_STREAM_SERVICE_UUID;
static const ble_uuid128_t s_notify_uuid = BLE_AUDIO_STREAM_NOTIFY_UUID;
static const ble_uuid128_t s_control_uuid = BLE_AUDIO_STREAM_CONTROL_UUID;
static const ble_uuid128_t s_readiness_uuid = BLE_AUDIO_STREAM_READINESS_UUID;
static const ble_uuid128_t s_capabilities_uuid = BLE_AUDIO_STREAM_CAPABILITIES_UUID;

typedef enum {
    BLE_AUDIO_STREAM_GATT_ATTR_NOTIFY = 0,
    BLE_AUDIO_STREAM_GATT_ATTR_CONTROL,
    BLE_AUDIO_STREAM_GATT_ATTR_READINESS,
    BLE_AUDIO_STREAM_GATT_ATTR_CAPABILITIES,
} ble_audio_stream_gatt_attr_t;

static uint16_t s_notify_attr_handle;
static bool s_started;
static bool s_registered;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_notify_enabled;
static bool s_mtu_ready;
static bool s_pending_subscribe_valid;
static uint16_t s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_pending_subscribe_epoch;
static bool s_pending_notify_enabled;
static bool s_pending_mtu_valid;
static uint16_t s_pending_mtu_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_pending_mtu_epoch;
static uint16_t s_pending_mtu_value;
static uint16_t s_packet_value_max_bytes = BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES;
static QueueHandle_t s_export_queue;
static TaskHandle_t s_export_task_handle;
static SemaphoreHandle_t s_notify_credit_sem;
static SemaphoreHandle_t s_notify_tx_done_sem;
static SemaphoreHandle_t s_audio_pool_mutex;
static uint8_t s_notify_window_depth = BLE_AUDIO_STREAM_NOTIFY_WINDOW_DEPTH;
static int s_last_notify_tx_status = INT_MIN;
static uint32_t s_connection_epoch;
static uint32_t s_notify_tx_wait_epoch;
static bool s_notify_tx_wait_active;
static ble_audio_stream_stale_event_counts_t s_stale_event_counts;
static ble_audio_stream_session_stats_t s_session_stats;
static ble_audio_stream_transport_state_t s_transport_state =
    BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED;
static uint32_t s_transport_session_id;
static uint16_t s_transport_expected_packet_count;
static uint16_t s_transport_audio_payload_bytes;
static uint8_t *s_audio_pool_storage;
static bool s_audio_pool_used[BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH];
static uint32_t s_audio_pool_in_use;
static uint32_t s_audio_pool_global_high_water;
static bool s_backpressure_active;
static ble_audio_stream_replay_packet_t s_replay_window[BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS];
static uint32_t s_replay_next_index;
static bool s_replay_pending;
static uint32_t s_replay_session_id;
static bool s_replay_in_progress;
static portMUX_TYPE s_backpressure_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_link_state_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_audio_stream_control_write_handler_t s_control_write_handler;

typedef struct {
    uint16_t conn_handle;
    bool mtu_ready;
    bool notify_enabled;
    uint16_t packet_value_max_bytes;
    uint32_t connection_epoch;
} ble_audio_stream_link_snapshot_t;

static ble_audio_stream_link_snapshot_t ble_audio_stream_get_link_snapshot(void);
static esp_err_t ble_audio_stream_send_packet(
    listener_audio_packet_type_t packet_type,
    uint32_t session_id,
    uint16_t sequence_or_count,
    uint8_t fragment_index,
    uint8_t fragment_count,
    const uint8_t *payload,
    uint16_t payload_len,
    uint16_t packet_pcm_bytes);

_Static_assert(
    LISTENER_AUDIO_PROTO_HEADER_BYTES <= sizeof(listener_audio_packet_header_t),
    "listener audio header constant exceeds struct size");
_Static_assert(
    BLE_AUDIO_STREAM_REPLAY_PAYLOAD_BYTES >= 480,
    "replay window must hold a full negotiated audio payload");

static uint16_t ble_audio_stream_packet_payload_bytes(void)
{
    uint16_t packet_value_max_bytes = ble_audio_stream_get_link_snapshot().packet_value_max_bytes;
    if (packet_value_max_bytes <= LISTENER_AUDIO_PROTO_HEADER_BYTES) {
        return 0;
    }
    return (uint16_t)(packet_value_max_bytes - LISTENER_AUDIO_PROTO_HEADER_BYTES);
}

uint16_t ble_audio_stream_get_audio_payload_bytes(void)
{
    return (uint16_t)(ble_audio_stream_packet_payload_bytes() & ~1u);
}

static uint16_t ble_audio_stream_session_audio_payload_bytes(void)
{
    if (s_transport_audio_payload_bytes != 0) {
        return s_transport_audio_payload_bytes;
    }
    return ble_audio_stream_get_audio_payload_bytes();
}

static bool ble_audio_stream_get_backpressure_active(void)
{
    portENTER_CRITICAL(&s_backpressure_lock);
    bool active = s_backpressure_active;
    portEXIT_CRITICAL(&s_backpressure_lock);
    return active;
}

static bool ble_audio_stream_set_backpressure_active(bool active)
{
    portENTER_CRITICAL(&s_backpressure_lock);
    bool changed = s_backpressure_active != active;
    s_backpressure_active = active;
    portEXIT_CRITICAL(&s_backpressure_lock);
    return changed;
}

static ble_audio_stream_link_snapshot_t ble_audio_stream_get_link_snapshot(void)
{
    portENTER_CRITICAL(&s_link_state_lock);
    ble_audio_stream_link_snapshot_t snapshot = {
        .conn_handle = s_conn_handle,
        .mtu_ready = s_mtu_ready,
        .notify_enabled = s_notify_enabled,
        .packet_value_max_bytes = s_packet_value_max_bytes,
        .connection_epoch = s_connection_epoch,
    };
    portEXIT_CRITICAL(&s_link_state_lock);
    return snapshot;
}

uint16_t ble_audio_stream_count_audio_packets(uint16_t pcm_bytes)
{
    uint16_t payload_bytes = ble_audio_stream_session_audio_payload_bytes();
    if (pcm_bytes == 0 || payload_bytes == 0) {
        return 0;
    }

    return (uint16_t)((pcm_bytes + payload_bytes - 1u) / payload_bytes);
}

static void ble_audio_stream_replay_clear_session(uint32_t session_id)
{
    if (session_id == 0) {
        return;
    }

    for (size_t i = 0; i < BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS; ++i) {
        if (s_replay_window[i].valid && s_replay_window[i].session_id == session_id) {
            memset(&s_replay_window[i], 0, sizeof(s_replay_window[i]));
        }
    }
    if (s_replay_pending && s_replay_session_id == session_id) {
        s_replay_pending = false;
        s_replay_session_id = 0;
    }
}

static uint32_t ble_audio_stream_replay_count_retained(uint32_t session_id)
{
    uint32_t retained = 0;
    for (size_t i = 0; i < BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS; ++i) {
        if (s_replay_window[i].valid && s_replay_window[i].session_id == session_id) {
            retained++;
        }
    }
    return retained;
}

static void ble_audio_stream_stats_begin(uint32_t session_id)
{
    ble_audio_stream_replay_clear_session(session_id);
    memset(&s_session_stats, 0, sizeof(s_session_stats));
    s_session_stats.active = true;
    s_session_stats.session_id = session_id;
}

static void ble_audio_stream_stats_retry(
    uint32_t session_id,
    int last_error,
    ble_audio_stream_retry_cause_t cause)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    s_session_stats.notify_retries++;
    s_session_stats.last_error = last_error;
    switch (cause) {
        case BLE_AUDIO_STREAM_RETRY_CAUSE_MBUF_ALLOC:
            s_session_stats.notify_mbuf_alloc_retries++;
            break;
        case BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_ENOMEM:
            s_session_stats.notify_enomem_retries++;
            break;
        case BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_TIMEOUT:
            s_session_stats.notify_tx_timeout_retries++;
            break;
        case BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_STATUS:
            s_session_stats.notify_tx_status_retries++;
            break;
        case BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_OTHER:
        default:
            s_session_stats.notify_other_retries++;
            break;
    }
}

static void ble_audio_stream_stats_packet_result(
    uint32_t session_id,
    listener_audio_packet_type_t packet_type,
    esp_err_t result)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    if (result == ESP_OK) {
        s_session_stats.notify_sent++;
        if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
            s_session_stats.audio_packets_sent++;
        }
        return;
    }

    s_session_stats.notify_failed++;
    s_session_stats.last_error = result;
    if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
        s_session_stats.audio_packets_failed++;
    }
}

static void ble_audio_stream_stats_purged(uint32_t session_id, uint32_t purged_count)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    s_session_stats.queue_jobs_purged += purged_count;
}

static void ble_audio_stream_stats_pool_high_water(uint32_t session_id, uint32_t in_use)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    if (in_use > s_session_stats.audio_pool_high_water) {
        s_session_stats.audio_pool_high_water = in_use;
    }

    if (!s_session_stats.audio_pool_pressure_warned &&
        in_use >= BLE_AUDIO_STREAM_AUDIO_POOL_PRESSURE_WARN_LEVEL) {
        s_session_stats.audio_pool_pressure_warned = true;
        ESP_LOGW(
            TAG,
            "audio buffer pool pressure high: session=%" PRIu32 " in_use=%" PRIu32 "/%u threshold=%u%%",
            session_id,
            in_use,
            (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
            (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_PRESSURE_WARN_PERCENT);
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_POOL_EXHAUST, DIAG_SEV_WARN,
                 session_id, in_use, BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH, 0);
    }
}

static uint32_t ble_audio_stream_pressure_percent(uint32_t queue_depth, uint32_t pool_in_use)
{
    uint32_t queue_percent = 0;
    uint32_t pool_percent = 0;

    if (BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH > 0) {
        queue_percent = (queue_depth * 100U) / BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH;
    }
    if (BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH > 0) {
        pool_percent = (pool_in_use * 100U) / BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH;
    }

    return queue_percent > pool_percent ? queue_percent : pool_percent;
}

static void ble_audio_stream_log_watermark(
    uint32_t session_id,
    uint32_t queue_depth,
    uint32_t pool_in_use,
    uint32_t pressure_percent)
{
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_WATERMARK, DIAG_SEV_INFO,
             session_id, queue_depth, pool_in_use, pressure_percent);
}

static void ble_audio_stream_stats_drop(uint32_t session_id, const char *reason, esp_err_t result)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    if (strcmp(reason, "pool_exhausted") == 0) {
        s_session_stats.audio_pool_alloc_failed++;
    } else if (strcmp(reason, "queue_full") == 0) {
        s_session_stats.audio_queue_full++;
    }
    s_session_stats.last_drop_reason = reason;
    s_session_stats.last_error = result;
}

static void ble_audio_stream_stats_log_and_end(
    uint32_t session_id,
    const char *reason,
    uint16_t expected_packet_count)
{
    if (!s_session_stats.active || s_session_stats.session_id != session_id) {
        return;
    }

    ESP_LOGI(
        TAG,
        "audio session transport summary: session=%" PRIu32 " reason=%s expected_packet_count=%u notify_sent=%" PRIu32 " notify_failed=%" PRIu32 " notify_retries=%" PRIu32 " retry_mbuf=%" PRIu32 " retry_enomem=%" PRIu32 " retry_tx_timeout=%" PRIu32 " retry_tx_status=%" PRIu32 " retry_other=%" PRIu32 " audio_sent=%" PRIu32 " audio_failed=%" PRIu32 " queue_jobs_purged=%" PRIu32 " pool_high_water=%" PRIu32 " pool_capacity=%u pool_high_water_pct=%" PRIu32 " pool_alloc_failed=%" PRIu32 " queue_full=%" PRIu32 " last_drop_reason=%s last_error=%d",
        session_id,
        reason,
        expected_packet_count,
        s_session_stats.notify_sent,
        s_session_stats.notify_failed,
        s_session_stats.notify_retries,
        s_session_stats.notify_mbuf_alloc_retries,
        s_session_stats.notify_enomem_retries,
        s_session_stats.notify_tx_timeout_retries,
        s_session_stats.notify_tx_status_retries,
        s_session_stats.notify_other_retries,
        s_session_stats.audio_packets_sent,
        s_session_stats.audio_packets_failed,
        s_session_stats.queue_jobs_purged,
        s_session_stats.audio_pool_high_water,
        (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
        (s_session_stats.audio_pool_high_water * 100U) / BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
        s_session_stats.audio_pool_alloc_failed,
        s_session_stats.audio_queue_full,
        s_session_stats.last_drop_reason != NULL ? s_session_stats.last_drop_reason : "none",
        s_session_stats.last_error);
    memset(&s_session_stats, 0, sizeof(s_session_stats));
}

static uint16_t ble_audio_stream_error_from_result(esp_err_t result)
{
    switch (result) {
        case ESP_ERR_TIMEOUT:
            return LISTENER_AUDIO_SESSION_ERROR_NOTIFY_TIMEOUT;
        case ESP_ERR_INVALID_STATE:
            return LISTENER_AUDIO_SESSION_ERROR_LINK_LOST;
        case ESP_ERR_INVALID_SIZE:
            return LISTENER_AUDIO_SESSION_ERROR_PACKET_TOO_LARGE;
        case ESP_ERR_NO_MEM:
            return LISTENER_AUDIO_SESSION_ERROR_NO_MEMORY;
        default:
            break;
    }

    return LISTENER_AUDIO_SESSION_ERROR_TRANSPORT;
}

static const char *ble_audio_stream_transport_state_name(ble_audio_stream_transport_state_t state)
{
    switch (state) {
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED:
            return "disconnected";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED:
            return "connected";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_MTU_READY:
            return "mtu_ready";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED:
            return "subscribed";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY:
            return "stream_ready";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING:
            return "streaming";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING:
            return "draining";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED:
            return "stopped";
        case BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR:
            return "error";
        default:
            break;
    }

    return "unknown";
}

static uint32_t ble_audio_stream_reason_code(const char *reason)
{
    uint32_t hash = 2166136261u;
    if (reason == NULL) {
        return 0;
    }
    while (*reason != '\0') {
        hash ^= (uint8_t)*reason;
        hash *= 16777619u;
        reason++;
    }
    return hash;
}

static void ble_audio_stream_set_transport_state(
    ble_audio_stream_transport_state_t next_state,
    const char *reason)
{
    if (s_transport_state == next_state) {
        return;
    }

    ble_audio_stream_transport_state_t previous_state = s_transport_state;
    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    ESP_LOGI(
        TAG,
        "audio transport state: %s -> %s reason=%s epoch=%" PRIu32 " conn=%u mtu_ready=%u notify=%u value_max=%u",
        ble_audio_stream_transport_state_name(previous_state),
        ble_audio_stream_transport_state_name(next_state),
        reason != NULL ? reason : "unspecified",
        link.connection_epoch,
        link.conn_handle,
        link.mtu_ready ? 1u : 0u,
        link.notify_enabled ? 1u : 0u,
        link.packet_value_max_bytes);
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_STATE_CHANGE, DIAG_SEV_INFO,
             (uint32_t)previous_state,
             (uint32_t)next_state,
             ble_audio_stream_reason_code(reason),
             s_transport_session_id);
    s_transport_state = next_state;
}

static void ble_audio_stream_log_notify_state(
    uint32_t notify_state,
    uint16_t conn_handle,
    uint8_t severity)
{
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_STATE, severity,
             s_transport_session_id,
             s_connection_epoch,
             conn_handle,
             notify_state);
}

static bool ble_audio_stream_transport_session_active(void)
{
    return s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING ||
           s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING;
}

static bool ble_audio_stream_transport_link_ready(void)
{
    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    return link.conn_handle != BLE_HS_CONN_HANDLE_NONE && link.mtu_ready && link.notify_enabled;
}

static uint32_t ble_audio_stream_next_epoch_value(void)
{
    uint32_t next_epoch = s_connection_epoch + 1U;
    return next_epoch == 0 ? 1U : next_epoch;
}

static uint32_t ble_audio_stream_advance_connection_epoch(const char *reason)
{
    portENTER_CRITICAL(&s_link_state_lock);
    s_connection_epoch = ble_audio_stream_next_epoch_value();
    uint32_t connection_epoch = s_connection_epoch;
    portEXIT_CRITICAL(&s_link_state_lock);
    ESP_LOGI(
        TAG,
        "audio transport epoch advanced: epoch=%" PRIu32 " reason=%s stale_subscribe=%" PRIu32 " stale_mtu=%" PRIu32 " stale_notify_tx=%" PRIu32 " stale_disconnect=%" PRIu32,
        connection_epoch,
        reason != NULL ? reason : "unspecified",
        s_stale_event_counts.subscribe,
        s_stale_event_counts.mtu,
        s_stale_event_counts.notify_tx,
        s_stale_event_counts.disconnect);
    return connection_epoch;
}

static void ble_audio_stream_note_stale_event(const char *event_type, uint16_t conn_handle, uint32_t event_epoch)
{
    if (strcmp(event_type, "subscribe") == 0) {
        s_stale_event_counts.subscribe++;
    } else if (strcmp(event_type, "mtu") == 0) {
        s_stale_event_counts.mtu++;
    } else if (strcmp(event_type, "notify_tx") == 0) {
        s_stale_event_counts.notify_tx++;
    } else if (strcmp(event_type, "disconnect") == 0) {
        s_stale_event_counts.disconnect++;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    ESP_LOGW(
        TAG,
        "audio stale event discarded: type=%s conn=%u event_epoch=%" PRIu32 " current_epoch=%" PRIu32 " current_conn=%u counts_subscribe=%" PRIu32 " counts_mtu=%" PRIu32 " counts_notify_tx=%" PRIu32 " counts_disconnect=%" PRIu32,
        event_type,
        conn_handle,
        event_epoch,
        link.connection_epoch,
        link.conn_handle,
        s_stale_event_counts.subscribe,
        s_stale_event_counts.mtu,
        s_stale_event_counts.notify_tx,
        s_stale_event_counts.disconnect);
}

static void ble_audio_stream_delay_ms(uint32_t delay_ms);

static bool ble_audio_stream_wait_link_ready(
    uint32_t session_id,
    listener_audio_packet_type_t packet_type,
    uint16_t sequence_or_count,
    uint8_t fragment_index,
    uint8_t fragment_count)
{
    if (ble_audio_stream_transport_link_ready()) {
        return true;
    }

    bool logged_wait = false;
    uint32_t waited_ms = 0;
    while (waited_ms < BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS) {
        if (!ble_audio_stream_transport_session_active() ||
            s_transport_session_id != session_id) {
            return false;
        }
        if (ble_audio_stream_transport_link_ready()) {
            if (logged_wait) {
                ESP_LOGI(
                    TAG,
                    "notify link recovered: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u waited_ms=%" PRIu32 " resume_delay_ms=%u",
                    (unsigned)packet_type,
                    session_id,
                    sequence_or_count,
                    fragment_index,
                    fragment_count,
                    waited_ms,
                    (unsigned)BLE_AUDIO_STREAM_LINK_RECOVERY_RESUME_DELAY_MS);
                ble_audio_stream_delay_ms(BLE_AUDIO_STREAM_LINK_RECOVERY_RESUME_DELAY_MS);
            }
            return true;
        }

        if (!logged_wait) {
            ESP_LOGW(
                TAG,
                "notify paused waiting for link recovery: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u timeout_ms=%u",
                (unsigned)packet_type,
                session_id,
                sequence_or_count,
                fragment_index,
                fragment_count,
                (unsigned)BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS);
            logged_wait = true;
        }
        ble_audio_stream_delay_ms(BLE_AUDIO_STREAM_LINK_RECOVERY_POLL_MS);
        waited_ms += BLE_AUDIO_STREAM_LINK_RECOVERY_POLL_MS;
    }

    ESP_LOGW(
        TAG,
        "notify link recovery timeout: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u waited_ms=%u",
        (unsigned)packet_type,
        session_id,
        sequence_or_count,
        fragment_index,
        fragment_count,
        (unsigned)BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS);
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_LINK_TIMEOUT, DIAG_SEV_WARN,
             session_id, BLE_AUDIO_STREAM_LINK_RECOVERY_WAIT_MS, packet_type, sequence_or_count);
    return false;
}

static void ble_audio_stream_refresh_link_state(const char *reason)
{
    if (ble_audio_stream_transport_session_active()) {
        return;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    if (link.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ble_audio_stream_set_transport_state(
            BLE_AUDIO_STREAM_TRANSPORT_STATE_DISCONNECTED,
            reason);
        return;
    }

    if (!link.mtu_ready) {
        ble_audio_stream_set_transport_state(
            BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED,
            reason);
        return;
    }

    if (!link.notify_enabled) {
        ble_audio_stream_set_transport_state(
            BLE_AUDIO_STREAM_TRANSPORT_STATE_MTU_READY,
            reason);
        return;
    }

    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_SUBSCRIBED,
        reason);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY,
        reason);
}

static void ble_audio_stream_reset_transport_session(void)
{
    ble_audio_stream_replay_clear_session(s_transport_session_id);
    s_transport_session_id = 0;
    s_transport_expected_packet_count = 0;
    s_transport_audio_payload_bytes = 0;
}

static void ble_audio_stream_note_transport_progress(uint32_t session_id, uint16_t expected_packet_count)
{
    if (s_transport_session_id != session_id) {
        return;
    }
    if (expected_packet_count > s_transport_expected_packet_count) {
        s_transport_expected_packet_count = expected_packet_count;
    }
}

static void ble_audio_stream_replay_store_packet(
    uint32_t session_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_len,
    uint16_t packet_pcm_bytes)
{
    if (session_id == 0 || payload == NULL || payload_len == 0 ||
        payload_len > BLE_AUDIO_STREAM_REPLAY_PAYLOAD_BYTES ||
        s_replay_in_progress) {
        return;
    }

    for (size_t i = 0; i < BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS; ++i) {
        ble_audio_stream_replay_packet_t *existing = &s_replay_window[i];
        if (existing->valid && existing->session_id == session_id &&
            existing->sequence == sequence) {
            existing->payload_len = payload_len;
            existing->packet_pcm_bytes = packet_pcm_bytes;
            memcpy(existing->payload, payload, payload_len);
            return;
        }
    }

    ble_audio_stream_replay_packet_t *slot =
        &s_replay_window[s_replay_next_index % BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS];
    memset(slot, 0, sizeof(*slot));
    slot->valid = true;
    slot->session_id = session_id;
    slot->sequence = sequence;
    slot->payload_len = payload_len;
    slot->packet_pcm_bytes = packet_pcm_bytes;
    memcpy(slot->payload, payload, payload_len);
    s_replay_next_index++;
}

static void ble_audio_stream_replay_remove_packet(uint32_t session_id, uint16_t sequence)
{
    if (session_id == 0) {
        return;
    }

    for (size_t i = 0; i < BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS; ++i) {
        ble_audio_stream_replay_packet_t *packet = &s_replay_window[i];
        if (packet->valid && packet->session_id == session_id &&
            packet->sequence == sequence) {
            memset(packet, 0, sizeof(*packet));
            return;
        }
    }
}

static void ble_audio_stream_replay_mark_link_suspended(const char *reason)
{
    if (!ble_audio_stream_transport_session_active()) {
        return;
    }

    uint32_t session_id = s_transport_session_id;
    uint32_t retained = ble_audio_stream_replay_count_retained(session_id);
    if (retained == 0) {
        return;
    }

    s_replay_pending = true;
    s_replay_session_id = session_id;
    ESP_LOGW(
        TAG,
        "audio replay window armed: reason=%s session=%" PRIu32 " retained=%" PRIu32 " window=%u",
        reason != NULL ? reason : "link_suspended",
        session_id,
        retained,
        (unsigned)BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS);
}

static int ble_audio_stream_replay_compare_sequence(const void *a, const void *b)
{
    const ble_audio_stream_replay_packet_t *left =
        *(const ble_audio_stream_replay_packet_t * const *)a;
    const ble_audio_stream_replay_packet_t *right =
        *(const ble_audio_stream_replay_packet_t * const *)b;
    if (left->sequence < right->sequence) {
        return -1;
    }
    if (left->sequence > right->sequence) {
        return 1;
    }
    return 0;
}

static esp_err_t ble_audio_stream_replay_pending_packets(
    uint32_t session_id,
    bool skip_current_sequence,
    uint16_t current_sequence)
{
    if (s_replay_in_progress || !s_replay_pending || s_replay_session_id != session_id) {
        return ESP_OK;
    }

    const ble_audio_stream_replay_packet_t *packets[BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS];
    size_t count = 0;
    bool skipped_current = false;
    for (size_t i = 0; i < BLE_AUDIO_STREAM_REPLAY_WINDOW_PACKETS; ++i) {
        if (s_replay_window[i].valid && s_replay_window[i].session_id == session_id) {
            if (skip_current_sequence && s_replay_window[i].sequence == current_sequence) {
                skipped_current = true;
                continue;
            }
            packets[count++] = &s_replay_window[i];
        }
    }

    if (count == 0) {
        s_replay_pending = false;
        s_replay_session_id = 0;
        if (skipped_current) {
            ESP_LOGW(
                TAG,
                "audio replay window skip current packet: session=%" PRIu32 " seq=%u",
                session_id,
                current_sequence);
        }
        return ESP_OK;
    }

    qsort(packets, count, sizeof(packets[0]), ble_audio_stream_replay_compare_sequence);
    ESP_LOGW(
        TAG,
        "audio replay window resend: session=%" PRIu32 " packets=%u first_seq=%u last_seq=%u",
        session_id,
        (unsigned)count,
        packets[0]->sequence,
        packets[count - 1]->sequence);
    if (skipped_current) {
        ESP_LOGW(
            TAG,
            "audio replay window skip current packet: session=%" PRIu32 " seq=%u",
            session_id,
            current_sequence);
    }

    s_replay_in_progress = true;
    for (size_t i = 0; i < count; ++i) {
        esp_err_t ret = ble_audio_stream_send_packet(
            LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA,
            session_id,
            packets[i]->sequence,
            0,
            1,
            packets[i]->payload,
            packets[i]->payload_len,
            packets[i]->packet_pcm_bytes);
        if (ret != ESP_OK) {
            s_replay_in_progress = false;
            return ret;
        }
    }
    s_replay_in_progress = false;
    s_replay_pending = false;
    s_replay_session_id = 0;
    return ESP_OK;
}

static void ble_audio_stream_delay_ms(uint32_t delay_ms)
{
    watchdog_platform_delay_ms(delay_ms);
}

static void ble_audio_stream_notify_success_delay(void)
{
    uint32_t queued_count =
        s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0;
    uint32_t delay_ms = queued_count >= BLE_AUDIO_STREAM_NOTIFY_BACKLOG_QUEUE_THRESHOLD
        ? BLE_AUDIO_STREAM_NOTIFY_BACKLOG_SUCCESS_DELAY_MS
        : BLE_AUDIO_STREAM_NOTIFY_SUCCESS_DELAY_MS;
    ble_audio_stream_delay_ms(delay_ms);
}

static void ble_audio_stream_notify_retry_delay(void)
{
    ble_audio_stream_delay_ms(BLE_AUDIO_STREAM_NOTIFY_RETRY_DELAY_MS);
}

static void ble_audio_stream_reset_notify_credit(void)
{
    if (s_notify_credit_sem == NULL) {
        return;
    }

    xQueueReset(s_notify_credit_sem);
    if (s_notify_tx_done_sem != NULL) {
        xQueueReset(s_notify_tx_done_sem);
    }
    s_last_notify_tx_status = INT_MIN;
    s_notify_tx_wait_active = false;
    s_notify_tx_wait_epoch = 0;
}

static void ble_audio_stream_prime_notify_credit(void)
{
    if (s_notify_credit_sem == NULL) {
        return;
    }

    xQueueReset(s_notify_credit_sem);
    for (uint8_t i = 0; i < s_notify_window_depth; ++i) {
        xSemaphoreGive(s_notify_credit_sem);
    }
}

static void ble_audio_stream_apply_notify_enabled(bool notify_enabled)
{
    portENTER_CRITICAL(&s_link_state_lock);
    s_notify_enabled = notify_enabled;
    portEXIT_CRITICAL(&s_link_state_lock);
    if (notify_enabled) {
        ble_audio_stream_prime_notify_credit();
    } else {
        ble_audio_stream_reset_notify_credit();
    }
}

static void ble_audio_stream_log_packet_size(uint16_t conn_handle)
{
    ESP_LOGI(
        TAG,
        "audio notify packet size updated: conn=%u value_max=%u payload_max=%u",
        conn_handle,
        s_packet_value_max_bytes,
        ble_audio_stream_packet_payload_bytes());
}

static void ble_audio_stream_apply_mtu_value(uint16_t conn_handle, uint16_t mtu)
{
    if (mtu <= 3) {
        return;
    }

    uint16_t value_max = (uint16_t)(mtu - 3);
    if (value_max > BLE_AUDIO_STREAM_PACKET_MAX_BYTES) {
        value_max = BLE_AUDIO_STREAM_PACKET_MAX_BYTES;
    }
    if (value_max < LISTENER_AUDIO_PROTO_HEADER_BYTES) {
        value_max = LISTENER_AUDIO_PROTO_HEADER_BYTES;
    }

    portENTER_CRITICAL(&s_link_state_lock);
    s_packet_value_max_bytes = value_max;
    s_mtu_ready = true;
    portEXIT_CRITICAL(&s_link_state_lock);
    ESP_LOGI(TAG, "audio notify mtu updated: conn=%u mtu=%u", conn_handle, mtu);
    ble_audio_stream_log_packet_size(conn_handle);
}

static esp_err_t ble_audio_stream_wait_notify_credit(
    listener_audio_packet_type_t packet_type,
    uint32_t session_id,
    uint16_t sequence_or_count,
    uint8_t fragment_index,
    uint8_t fragment_count)
{
    if (s_notify_credit_sem == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_notify_credit_sem, pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_WAIT_MS)) == pdTRUE) {
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "notify credit timeout: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u window=%u",
        (unsigned)packet_type,
        session_id,
        sequence_or_count,
        fragment_index,
        fragment_count,
        s_notify_window_depth);
    return ESP_ERR_TIMEOUT;
}

void ble_audio_stream_set_control_write_handler(ble_audio_stream_control_write_handler_t handler)
{
    s_control_write_handler = handler;
}

static int ble_audio_stream_copy_control_mbuf(
    struct os_mbuf *om,
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t *out_len)
{
    if (om == NULL || buffer == NULL || out_len == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0 || (size_t)len > buffer_size) {
        ESP_LOGW(TAG, "audio control write invalid length: len=%u max=%u",
                 len, (unsigned)buffer_size);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(om, 0, len, buffer) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    *out_len = len;
    return 0;
}

static int ble_audio_stream_handle_control_write(struct os_mbuf *om)
{
    uint8_t buffer[BLE_AUDIO_STREAM_CONTROL_MAX_BYTES + 1];
    uint16_t len = 0;
    int att_err = ble_audio_stream_copy_control_mbuf(
        om,
        buffer,
        BLE_AUDIO_STREAM_CONTROL_MAX_BYTES,
        &len);
    if (att_err != 0) {
        return att_err;
    }
    buffer[len] = '\0';

    if (s_control_write_handler == NULL) {
        ESP_LOGW(TAG, "audio control write dropped: no handler payload=%s", (const char *)buffer);
        return BLE_ATT_ERR_UNLIKELY;
    }

    esp_err_t ret = s_control_write_handler(buffer, len, "ble_audio_control");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "audio control write failed: payload=%s ret=%s",
                 (const char *)buffer, esp_err_to_name(ret));
        return ret == ESP_ERR_INVALID_ARG ? BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN : BLE_ATT_ERR_UNLIKELY;
    }
    ESP_LOGI(TAG, "audio control write handled: payload=%s", (const char *)buffer);
    return 0;
}

static int ble_audio_stream_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;

    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ble_audio_stream_gatt_attr_t attr = (ble_audio_stream_gatt_attr_t)(uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attr == BLE_AUDIO_STREAM_GATT_ATTR_CONTROL) {
            return ble_audio_stream_handle_control_write(ctxt->om);
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    const char *value = NULL;
    switch (attr) {
    case BLE_AUDIO_STREAM_GATT_ATTR_READINESS:
        value = listener_device_get_factory_readiness();
        break;
    case BLE_AUDIO_STREAM_GATT_ATTR_CAPABILITIES:
        value = listener_device_get_capabilities();
        break;
    case BLE_AUDIO_STREAM_GATT_ATTR_CONTROL:
    case BLE_AUDIO_STREAM_GATT_ATTR_NOTIFY:
    default:
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    int rc = os_mbuf_append(ctxt->om, value, strlen(value));
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    ESP_LOGI(TAG, "factory info read: attr=%u value=%s",
             (unsigned)((ble_audio_stream_gatt_attr_t)(uintptr_t)arg),
             value);
    return 0;
}

static const struct ble_gatt_svc_def s_audio_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_notify_uuid.u,
                .access_cb = ble_audio_stream_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_notify_attr_handle,
                .arg = (void *)(uintptr_t)BLE_AUDIO_STREAM_GATT_ATTR_NOTIFY,
            },
            {
                .uuid = &s_control_uuid.u,
                .access_cb = ble_audio_stream_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_AUDIO_STREAM_GATT_ATTR_CONTROL,
            },
            {
                .uuid = &s_readiness_uuid.u,
                .access_cb = ble_audio_stream_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_AUDIO_STREAM_GATT_ATTR_READINESS,
            },
            {
                .uuid = &s_capabilities_uuid.u,
                .access_cb = ble_audio_stream_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_AUDIO_STREAM_GATT_ATTR_CAPABILITIES,
            },
            {0},
        },
    },
    {0},
};

static esp_err_t ble_audio_stream_send_packet(
    listener_audio_packet_type_t packet_type,
    uint32_t session_id,
    uint16_t sequence_or_count,
    uint8_t fragment_index,
    uint8_t fragment_count,
    const uint8_t *payload,
    uint16_t payload_len,
    uint16_t packet_pcm_bytes)
{
    if (!ble_audio_stream_transport_session_active() ||
        !ble_audio_stream_wait_link_ready(
            session_id,
            packet_type,
            sequence_or_count,
            fragment_index,
            fragment_count)) {
        ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    uint16_t packet_len = (uint16_t)(LISTENER_AUDIO_PROTO_HEADER_BYTES + payload_len);
    if (packet_len > link.packet_value_max_bytes || packet_len > BLE_AUDIO_STREAM_PACKET_MAX_BYTES) {
        ESP_LOGW(
            TAG,
            "notify packet too large: type=%u session=%" PRIu32 " seq_or_count=%u len=%u max=%u",
            (unsigned)packet_type,
            session_id,
            sequence_or_count,
            packet_len,
            link.packet_value_max_bytes);
        ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    bool skip_replay_current_packet = false;
    if (!s_replay_in_progress &&
        packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA &&
        s_replay_pending &&
        s_replay_session_id == session_id) {
        ble_audio_stream_replay_store_packet(
            session_id,
            sequence_or_count,
            payload,
            payload_len,
            packet_pcm_bytes);
        skip_replay_current_packet = true;
    }

    if (!s_replay_in_progress &&
        (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA ||
         packet_type == LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP)) {
        esp_err_t replay_ret = ble_audio_stream_replay_pending_packets(
            session_id,
            skip_replay_current_packet,
            sequence_or_count);
        if (replay_ret != ESP_OK) {
            ble_audio_stream_stats_packet_result(session_id, packet_type, replay_ret);
            return replay_ret;
        }
    }

    uint8_t packet[BLE_AUDIO_STREAM_PACKET_MAX_BYTES];
    listener_audio_packet_header_t header;
    listener_audio_proto_header_init(
        &header,
        packet_type,
        session_id,
        sequence_or_count,
        fragment_index,
        fragment_count,
        payload_len,
        packet_pcm_bytes);

    memcpy(packet, &header, LISTENER_AUDIO_PROTO_HEADER_BYTES);
    if (payload_len > 0 && payload != NULL) {
        memcpy(packet + LISTENER_AUDIO_PROTO_HEADER_BYTES, payload, payload_len);
    }

    for (int attempt = 0; attempt < BLE_AUDIO_STREAM_NOTIFY_RETRY_LIMIT; ++attempt) {
        if (!ble_audio_stream_wait_link_ready(
                session_id,
                packet_type,
                sequence_or_count,
                fragment_index,
                fragment_count)) {
            ESP_LOGW(TAG, "notify aborted: link recovery unavailable");
            ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_ERR_INVALID_STATE);
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t credit_err = ble_audio_stream_wait_notify_credit(
            packet_type,
            session_id,
            sequence_or_count,
            fragment_index,
            fragment_count);
        if (credit_err != ESP_OK) {
            ble_audio_stream_stats_packet_result(session_id, packet_type, credit_err);
            return credit_err;
        }

        struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
        if (om == NULL) {
            ble_audio_stream_stats_retry(
                session_id,
                ESP_ERR_NO_MEM,
                BLE_AUDIO_STREAM_RETRY_CAUSE_MBUF_ALLOC);
            xSemaphoreGive(s_notify_credit_sem);
            ble_audio_stream_notify_retry_delay();
            continue;
        }

        if (s_notify_tx_done_sem != NULL) {
            xQueueReset(s_notify_tx_done_sem);
        }
        s_last_notify_tx_status = INT_MIN;
        link = ble_audio_stream_get_link_snapshot();
        s_notify_tx_wait_epoch = link.connection_epoch;
        s_notify_tx_wait_active = true;

        if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
            ble_audio_stream_replay_store_packet(
                session_id,
                sequence_or_count,
                payload,
                payload_len,
                packet_pcm_bytes);
        }

        int rc = ble_gatts_notify_custom(link.conn_handle, s_notify_attr_handle, om);
        if (rc == 0) {
            if (s_notify_tx_done_sem != NULL) {
                if (xSemaphoreTake(
                        s_notify_tx_done_sem,
                        pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_TX_DONE_WAIT_MS)) != pdTRUE) {
                    s_notify_tx_wait_active = false;
                    ESP_LOGW(
                        TAG,
                        "notify tx completion timeout: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u",
                        (unsigned)packet_type,
                        session_id,
                        sequence_or_count,
                        fragment_index,
                        fragment_count);
                    ble_audio_stream_stats_retry(
                        session_id,
                        ESP_ERR_TIMEOUT,
                        BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_TIMEOUT);
                    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_FAIL, DIAG_SEV_WARN,
                             session_id, sequence_or_count, ESP_ERR_TIMEOUT, s_session_stats.notify_retries);
                    if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
                        ble_audio_stream_replay_remove_packet(session_id, sequence_or_count);
                    }
                    xSemaphoreGive(s_notify_credit_sem);
                    ble_audio_stream_notify_retry_delay();
                    continue;
                }
                s_notify_tx_wait_active = false;

                if (s_last_notify_tx_status != 0 && s_last_notify_tx_status != BLE_HS_EDONE) {
                    if (s_last_notify_tx_status == BLE_HS_ENOMEM) {
                        if (s_session_stats.active && s_session_stats.session_id == session_id) {
                            s_session_stats.last_error = s_last_notify_tx_status;
                        }
                    } else {
                        ble_audio_stream_stats_retry(
                            session_id,
                            s_last_notify_tx_status,
                            BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_TX_STATUS);
                        ESP_LOGW(
                            TAG,
                            "notify tx completion retry: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u status=%d retries=%" PRIu32,
                            (unsigned)packet_type,
                            session_id,
                            sequence_or_count,
                            fragment_index,
                            fragment_count,
                            s_last_notify_tx_status,
                            s_session_stats.notify_retries);
                        if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
                            ble_audio_stream_replay_remove_packet(session_id, sequence_or_count);
                        }
                        ble_audio_stream_notify_retry_delay();
                        continue;
                    }
                }
            } else {
                s_notify_tx_wait_active = false;
            }
            ble_audio_stream_notify_success_delay();
            if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
                ble_audio_stream_replay_remove_packet(session_id, sequence_or_count);
            }
            ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_OK);
            return ESP_OK;
        }

        s_notify_tx_wait_active = false;
        os_mbuf_free_chain(om);
        if (rc == BLE_HS_ENOMEM) {
            if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
                ble_audio_stream_replay_remove_packet(session_id, sequence_or_count);
            }
            ble_audio_stream_stats_retry(
                session_id,
                rc,
                BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_ENOMEM);
            if ((attempt + 1) % BLE_AUDIO_STREAM_NOTIFY_RETRY_LOG_INTERVAL == 0) {
                ESP_LOGW(
                    TAG,
                    "notify backpressure continuing: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u attempts=%d queued=%" PRIu32 "/%" PRIu32,
                    (unsigned)packet_type,
                    session_id,
                    sequence_or_count,
                    fragment_index,
                    fragment_count,
                    attempt + 1,
                    s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
                    (uint32_t)BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH);
            }
            xSemaphoreGive(s_notify_credit_sem);
            ble_audio_stream_notify_retry_delay();
            continue;
        }

        if (s_notify_credit_sem != NULL) {
            xSemaphoreGive(s_notify_credit_sem);
        }
        if (packet_type == LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA) {
            ble_audio_stream_replay_remove_packet(session_id, sequence_or_count);
        }

        ESP_LOGW(
            TAG,
            "notify failed: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u rc=%d",
            (unsigned)packet_type,
            session_id,
            sequence_or_count,
            fragment_index,
            fragment_count,
            rc);
        ble_audio_stream_stats_retry(
            session_id,
            rc,
            BLE_AUDIO_STREAM_RETRY_CAUSE_NOTIFY_OTHER);
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_FAIL, DIAG_SEV_WARN,
                 session_id, sequence_or_count, (uint32_t)rc, s_session_stats.notify_retries);
        ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_FAIL);
        return ESP_FAIL;
    }

    ESP_LOGW(
        TAG,
        "notify failed after retries: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u retry_limit=%d queued=%" PRIu32 "/%" PRIu32,
        (unsigned)packet_type,
        session_id,
        sequence_or_count,
        fragment_index,
        fragment_count,
        BLE_AUDIO_STREAM_NOTIFY_RETRY_LIMIT,
        s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
        (uint32_t)BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH);
    ble_audio_stream_stats_packet_result(session_id, packet_type, ESP_ERR_TIMEOUT);
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_FAIL, DIAG_SEV_WARN,
             session_id, sequence_or_count, ESP_ERR_TIMEOUT, s_session_stats.notify_retries);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ble_audio_stream_send_control_packet_repeated(
    listener_audio_packet_type_t packet_type,
    uint32_t session_id,
    uint16_t sequence_or_count,
    uint16_t packet_pcm_bytes)
{
    bool sent_at_least_once = false;
    esp_err_t first_error = ESP_OK;

    for (uint8_t i = 0; i < BLE_AUDIO_STREAM_CONTROL_NOTIFY_REPETITIONS; ++i) {
        esp_err_t ret = ble_audio_stream_send_packet(
            packet_type,
            session_id,
            sequence_or_count,
            0,
            1,
            NULL,
            0,
            packet_pcm_bytes);
        if (ret == ESP_OK) {
            sent_at_least_once = true;
        } else if (first_error == ESP_OK) {
            first_error = ret;
        }

        if (i + 1 < BLE_AUDIO_STREAM_CONTROL_NOTIFY_REPETITIONS) {
            ble_audio_stream_delay_ms(BLE_AUDIO_STREAM_CONTROL_NOTIFY_REPEAT_DELAY_MS);
        }
    }

    return sent_at_least_once ? ESP_OK : first_error;
}

static esp_err_t ble_audio_stream_audio_pool_init(void)
{
    if (s_audio_pool_storage != NULL) {
        return ESP_OK;
    }

    if (s_audio_pool_mutex == NULL) {
        s_audio_pool_mutex = xSemaphoreCreateMutex();
        if (s_audio_pool_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    size_t total_bytes =
        (size_t)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH *
        (size_t)BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES;
    const char *heap_name = "spiram";
    s_audio_pool_storage = (uint8_t *)heap_caps_calloc(
        BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
        BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_audio_pool_storage == NULL) {
        heap_name = "8bit";
        s_audio_pool_storage = (uint8_t *)heap_caps_calloc(
            BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
            BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES,
            MALLOC_CAP_8BIT);
    }
    if (s_audio_pool_storage == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_audio_pool_used, 0, sizeof(s_audio_pool_used));
    s_audio_pool_in_use = 0;
    s_audio_pool_global_high_water = 0;
    ESP_LOGI(
        TAG,
        "audio buffer pool initialized: buffers=%u buffer_bytes=%u total_bytes=%u heap=%s",
        (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
        (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES,
        (unsigned)total_bytes,
        heap_name);
    return ESP_OK;
}

static void ble_audio_stream_audio_pool_deinit(void)
{
    if (s_audio_pool_storage != NULL) {
        heap_caps_free(s_audio_pool_storage);
        s_audio_pool_storage = NULL;
    }
    memset(s_audio_pool_used, 0, sizeof(s_audio_pool_used));
    s_audio_pool_in_use = 0;
    s_audio_pool_global_high_water = 0;
    if (s_audio_pool_mutex != NULL) {
        vSemaphoreDelete(s_audio_pool_mutex);
        s_audio_pool_mutex = NULL;
    }
}

static uint8_t *ble_audio_stream_audio_pool_buffer(size_t index)
{
    return s_audio_pool_storage +
           (index * (size_t)BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES);
}

static esp_err_t ble_audio_stream_audio_pool_acquire(
    uint32_t session_id,
    uint16_t pcm_bytes,
    uint8_t **out_buffer)
{
    if (out_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_buffer = NULL;

    if (pcm_bytes > BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES) {
        ble_audio_stream_stats_drop(session_id, "packet_too_large", ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_audio_pool_storage == NULL || s_audio_pool_mutex == NULL) {
        ble_audio_stream_stats_drop(session_id, "pool_unavailable", ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_audio_pool_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ble_audio_stream_stats_drop(session_id, "pool_lock_timeout", ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_ERR_TIMEOUT;
    for (size_t i = 0; i < BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH; ++i) {
        if (s_audio_pool_used[i]) {
            continue;
        }
        s_audio_pool_used[i] = true;
        s_audio_pool_in_use++;
        if (s_audio_pool_in_use > s_audio_pool_global_high_water) {
            s_audio_pool_global_high_water = s_audio_pool_in_use;
        }
        ble_audio_stream_stats_pool_high_water(session_id, s_audio_pool_in_use);
        *out_buffer = ble_audio_stream_audio_pool_buffer(i);
        ret = ESP_OK;
        break;
    }

    if (ret != ESP_OK) {
        ble_audio_stream_stats_drop(session_id, "pool_exhausted", ESP_ERR_TIMEOUT);
        ESP_LOGW(
            TAG,
            "audio buffer pool exhausted: session=%" PRIu32 " in_use=%" PRIu32 "/%u global_high_water=%" PRIu32,
            session_id,
            s_audio_pool_in_use,
            (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
            s_audio_pool_global_high_water);
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_POOL_EXHAUST, DIAG_SEV_WARN,
                 session_id, s_audio_pool_in_use, BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH, s_audio_pool_global_high_water);
    }

    xSemaphoreGive(s_audio_pool_mutex);
    return ret;
}

static bool ble_audio_stream_audio_pool_release(uint8_t *buffer)
{
    if (buffer == NULL || s_audio_pool_storage == NULL || s_audio_pool_mutex == NULL) {
        return false;
    }

    uintptr_t start = (uintptr_t)s_audio_pool_storage;
    uintptr_t end = start +
        ((uintptr_t)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH *
         (uintptr_t)BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES);
    uintptr_t value = (uintptr_t)buffer;
    if (value < start || value >= end) {
        return false;
    }

    uintptr_t offset = value - start;
    if ((offset % BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES) != 0) {
        return false;
    }

    size_t index = (size_t)(offset / BLE_AUDIO_STREAM_AUDIO_POOL_BUFFER_BYTES);
    if (index >= BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH) {
        return false;
    }

    if (xSemaphoreTake(s_audio_pool_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (s_audio_pool_used[index]) {
        s_audio_pool_used[index] = false;
        if (s_audio_pool_in_use > 0) {
            s_audio_pool_in_use--;
        }
    } else {
        ESP_LOGW(TAG, "audio buffer pool double release ignored: index=%u", (unsigned)index);
    }
    xSemaphoreGive(s_audio_pool_mutex);
    return true;
}

static void ble_audio_stream_free_job(ble_audio_stream_job_t *job)
{
    if (job == NULL) {
        return;
    }
    if (job->owned_pcm != NULL && !ble_audio_stream_audio_pool_release(job->owned_pcm)) {
        free(job->owned_pcm);
    }
    memset(job, 0, sizeof(*job));
}

static void ble_audio_stream_purge_queued_session_jobs(uint32_t session_id, bool keep_session_start)
{
    if (s_export_queue == NULL) {
        return;
    }

    UBaseType_t queued_count = uxQueueMessagesWaiting(s_export_queue);
    uint16_t dropped_count = 0;
    for (UBaseType_t i = 0; i < queued_count; ++i) {
        ble_audio_stream_job_t job = {0};
        if (xQueueReceive(s_export_queue, &job, 0) != pdTRUE) {
            break;
        }

        bool drop_job = job.session_id == session_id &&
            ((job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START && !keep_session_start) ||
             job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA ||
             job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP ||
             job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_FINALIZE_STOP ||
             job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL ||
             job.type == BLE_AUDIO_STREAM_JOB_TYPE_SESSION_ERROR);
        if (drop_job) {
            ble_audio_stream_free_job(&job);
            dropped_count++;
            continue;
        }

        if (xQueueSendToBack(s_export_queue, &job, 0) != pdTRUE) {
            ESP_LOGW(TAG, "failed to restore queued audio job while purging session=%" PRIu32, session_id);
            ble_audio_stream_free_job(&job);
        }
    }

    if (dropped_count > 0) {
        ble_audio_stream_stats_purged(session_id, dropped_count);
        ESP_LOGI(
            TAG,
            "purged queued audio jobs for canceled session: session=%" PRIu32 " dropped=%u",
            session_id,
            dropped_count);
    }
}

static void ble_audio_stream_abort_active_session(const char *reason)
{
    if (!ble_audio_stream_transport_session_active()) {
        return;
    }

    uint32_t session_id = s_transport_session_id;
    uint16_t expected_packet_count = s_transport_expected_packet_count;
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR,
        reason);
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_SESSION_ABORT, DIAG_SEV_ERROR,
             session_id, ble_audio_stream_reason_code(reason), expected_packet_count, s_connection_epoch);
    if (session_id != 0) {
        ble_audio_stream_purge_queued_session_jobs(session_id, false);
        ble_audio_stream_stats_log_and_end(
            session_id,
            reason != NULL ? reason : "transport_error",
            expected_packet_count);
    }
    ble_audio_stream_set_backpressure_active(false);
    ble_audio_stream_reset_transport_session();
}

static esp_err_t ble_audio_stream_send_session_start_internal(uint32_t session_id)
{
    ble_audio_stream_set_backpressure_active(false);
    return ble_audio_stream_send_control_packet_repeated(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_START,
        session_id,
        0,
        0);
}

static esp_err_t ble_audio_stream_send_session_audio_internal(
    uint32_t session_id,
    uint16_t packet_sequence,
    const uint8_t *pcm_buffer,
    uint16_t pcm_bytes)
{
    if (pcm_buffer == NULL || pcm_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t payload_bytes = ble_audio_stream_session_audio_payload_bytes();
    uint16_t packet_count = ble_audio_stream_count_audio_packets(pcm_bytes);
    if (payload_bytes == 0 || packet_count == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if ((uint32_t)packet_sequence + packet_count > UINT16_MAX) {
        ESP_LOGW(
            TAG,
            "audio data packet sequence overflow: session=%" PRIu32 " seq=%u add=%u",
            session_id,
            packet_sequence,
            packet_count);
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t dropped_packet_count = 0;
    esp_err_t last_packet_error = ESP_OK;
    for (uint16_t packet_offset = 0; packet_offset < packet_count; ++packet_offset) {
        size_t payload_offset = (size_t)packet_offset * payload_bytes;
        uint16_t payload_len = (uint16_t)(pcm_bytes - payload_offset);
        if (payload_len > payload_bytes) {
            payload_len = payload_bytes;
        }

        esp_err_t err = ble_audio_stream_send_packet(
            LISTENER_AUDIO_PACKET_TYPE_AUDIO_DATA,
            session_id,
            (uint16_t)(packet_sequence + packet_offset),
            0,
            1,
            pcm_buffer + payload_offset,
            payload_len,
            payload_len);
        if (err == ESP_OK) {
            continue;
        }

        if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_INVALID_SIZE || err == ESP_ERR_INVALID_ARG) {
            return err;
        }

        dropped_packet_count++;
        last_packet_error = err;
        ESP_LOGW(
            TAG,
            "audio data packet dropped: session=%" PRIu32 " seq=%u pcm_bytes=%u ret=%s",
            session_id,
            (unsigned)(packet_sequence + packet_offset),
            payload_len,
            esp_err_to_name(err));
        ble_audio_stream_notify_retry_delay();
    }

    if (dropped_packet_count > 0) {
        ESP_LOGW(
            TAG,
            "audio data batch failed with drops: session=%" PRIu32 " seq_start=%u packet_count=%u sent=%u dropped=%u last_ret=%s",
            session_id,
            packet_sequence,
            packet_count,
            packet_count - dropped_packet_count,
            dropped_packet_count,
            esp_err_to_name(last_packet_error));
        return last_packet_error;
    }

    return ESP_OK;
}

static esp_err_t ble_audio_stream_send_session_stop_internal(uint32_t session_id, uint16_t expected_packet_count)
{
    ESP_LOGI(
        TAG,
        "audio session stop: session=%" PRIu32 " expected_packet_count=%u",
        session_id,
        expected_packet_count);
    esp_err_t ret = ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        0);
    return ret;
}

static void ble_audio_stream_finalize_session_stop_internal(
    uint32_t session_id,
    uint16_t expected_packet_count)
{
    ble_audio_stream_log_watermark(
        session_id,
        s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
        s_audio_pool_in_use,
        ble_audio_stream_pressure_percent(
            s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
            s_audio_pool_in_use));
    ble_audio_stream_set_backpressure_active(false);
    ble_audio_stream_stats_log_and_end(session_id, "stop", expected_packet_count);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED,
        "session_stop_drained");
    ble_audio_stream_reset_transport_session();
    ble_audio_stream_refresh_link_state("session_stop_complete");
}

static esp_err_t ble_audio_stream_send_session_cancel_internal(uint32_t session_id, uint16_t expected_packet_count)
{
    ESP_LOGI(
        TAG,
        "audio session cancel: session=%" PRIu32 " expected_packet_count=%u",
        session_id,
        expected_packet_count);
    esp_err_t ret = ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_CANCEL,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        0);
    ble_audio_stream_set_backpressure_active(false);
    ble_audio_stream_stats_log_and_end(session_id, "cancel", expected_packet_count);
    ble_audio_stream_set_transport_state(
        ret == ESP_OK ? BLE_AUDIO_STREAM_TRANSPORT_STATE_STOPPED : BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR,
        ret == ESP_OK ? "session_cancel_sent" : "session_cancel_failed");
    ble_audio_stream_reset_transport_session();
    ble_audio_stream_refresh_link_state("session_cancel_complete");
    return ret;
}

static esp_err_t ble_audio_stream_send_session_error_internal(
    uint32_t session_id,
    uint16_t expected_packet_count,
    uint16_t error_code)
{
    ESP_LOGW(
        TAG,
        "audio session error: session=%" PRIu32 " expected_packet_count=%u error_code=%u",
        session_id,
        expected_packet_count,
        error_code);
    esp_err_t ret = ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_ERROR,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        error_code);
    ble_audio_stream_stats_log_and_end(session_id, "error", expected_packet_count);
    diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_SESSION_ABORT, DIAG_SEV_ERROR,
             session_id, error_code, expected_packet_count, s_connection_epoch);
    ble_audio_stream_set_backpressure_active(false);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_ERROR,
        ret == ESP_OK ? "session_error_sent" : "session_error_failed");
    ble_audio_stream_reset_transport_session();
    ble_audio_stream_refresh_link_state("session_error_complete");
    return ret;
}

static void ble_audio_stream_task(void *parameter)
{
    (void)parameter;
    (void)watchdog_platform_subscribe_current_task("ble_audio_stream_task");

    ble_audio_stream_job_t job;
    while (1) {
        watchdog_platform_feed_current_task();
        if (xQueueReceive(s_export_queue, &job, pdMS_TO_TICKS(BLE_AUDIO_STREAM_TASK_QUEUE_WAIT_MS)) != pdTRUE) {
            continue;
        }
        switch (job.type) {
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START:
            {
                esp_err_t start_ret = ble_audio_stream_send_session_start_internal(job.session_id);
                if (start_ret != ESP_OK) {
                    ble_audio_stream_abort_active_session("session_start_failed");
                }
                break;
            }
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA:
            {
                esp_err_t audio_ret = ble_audio_stream_send_session_audio_internal(
                    job.session_id,
                    job.packet_sequence,
                    job.owned_pcm,
                    job.pcm_bytes);
                if (audio_ret != ESP_OK) {
                    uint16_t packet_count = ble_audio_stream_count_audio_packets(job.pcm_bytes);
                    uint16_t expected_packet_count = job.packet_sequence;
                    if ((uint32_t)expected_packet_count + packet_count <= UINT16_MAX) {
                        expected_packet_count = (uint16_t)(expected_packet_count + packet_count);
                    }
                    ble_audio_stream_purge_queued_session_jobs(job.session_id, true);
                    ble_audio_stream_set_transport_state(
                        BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
                        "session_audio_failed");
                    ble_audio_stream_send_session_error_internal(
                        job.session_id,
                        expected_packet_count,
                        ble_audio_stream_error_from_result(audio_ret));
                }
                break;
            }
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP:
            {
                esp_err_t stop_ret = ble_audio_stream_send_session_stop_internal(
                    job.session_id,
                    job.expected_packet_count);
                if (stop_ret != ESP_OK) {
                    ble_audio_stream_abort_active_session("session_stop_failed");
                }
                break;
            }
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_FINALIZE_STOP:
                ble_audio_stream_finalize_session_stop_internal(
                    job.session_id,
                    job.expected_packet_count);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL:
                ble_audio_stream_send_session_cancel_internal(job.session_id, job.expected_packet_count);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_ERROR:
                ble_audio_stream_send_session_error_internal(
                    job.session_id,
                    job.expected_packet_count,
                    job.error_code);
                break;
            default:
                break;
        }

        ble_audio_stream_free_job(&job);
        watchdog_platform_feed_current_task();
    }
}

esp_err_t ble_audio_stream_register_gatt(void)
{
    if (s_registered) {
        return ESP_OK;
    }

    int rc = ble_gatts_count_cfg(s_audio_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_audio_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    s_registered = true;
    ESP_LOGI(TAG, "audio GATT service registered");
    return ESP_OK;
}

void ble_audio_stream_log_gatt_state(void)
{
    uint16_t service_handle = 0;
    uint16_t chr_def_handle = 0;
    uint16_t chr_val_handle = 0;
    int svc_rc = ble_gatts_find_svc(&s_service_uuid.u, &service_handle);
    int chr_rc = ble_gatts_find_chr(&s_service_uuid.u, &s_notify_uuid.u, &chr_def_handle, &chr_val_handle);

    ESP_LOGI(
        TAG,
        "audio GATT state: registered=%u svc_rc=%d svc_handle=%u chr_rc=%d chr_def=%u chr_val=%u notify_attr=%u",
        s_registered,
        svc_rc,
        service_handle,
        chr_rc,
        chr_def_handle,
        chr_val_handle,
        s_notify_attr_handle);
}

esp_err_t ble_audio_stream_init(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_export_queue = xQueueCreate(BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH, sizeof(ble_audio_stream_job_t));
    if (s_export_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_notify_credit_sem = xSemaphoreCreateCounting(BLE_AUDIO_STREAM_NOTIFY_WINDOW_DEPTH, 0);
    if (s_notify_credit_sem == NULL) {
        vQueueDelete(s_export_queue);
        s_export_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_notify_tx_done_sem = xSemaphoreCreateBinary();
    if (s_notify_tx_done_sem == NULL) {
        vSemaphoreDelete(s_notify_credit_sem);
        s_notify_credit_sem = NULL;
        vQueueDelete(s_export_queue);
        s_export_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t pool_ret = ble_audio_stream_audio_pool_init();
    if (pool_ret != ESP_OK) {
        ble_audio_stream_audio_pool_deinit();
        vSemaphoreDelete(s_notify_tx_done_sem);
        s_notify_tx_done_sem = NULL;
        vSemaphoreDelete(s_notify_credit_sem);
        s_notify_credit_sem = NULL;
        vQueueDelete(s_export_queue);
        s_export_queue = NULL;
        return pool_ret;
    }

    BaseType_t task_ok = xTaskCreate(
        ble_audio_stream_task,
        "ble_audio_stream_task",
        BLE_AUDIO_STREAM_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 5,
        &s_export_task_handle);
    if (task_ok != pdPASS) {
        ble_audio_stream_audio_pool_deinit();
        vSemaphoreDelete(s_notify_tx_done_sem);
        s_notify_tx_done_sem = NULL;
        vSemaphoreDelete(s_notify_credit_sem);
        s_notify_credit_sem = NULL;
        vQueueDelete(s_export_queue);
        s_export_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

void ble_audio_stream_on_gap_connect(uint16_t conn_handle)
{
    uint32_t epoch = ble_audio_stream_advance_connection_epoch("gap_connect");
    portENTER_CRITICAL(&s_link_state_lock);
    s_conn_handle = conn_handle;
    s_mtu_ready = false;
    s_notify_enabled = false;
    s_packet_value_max_bytes = BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES;
    portEXIT_CRITICAL(&s_link_state_lock);
    ble_audio_stream_reset_notify_credit();
    if (ble_audio_stream_transport_session_active()) {
        ESP_LOGI(
            TAG,
            "audio transport link reconnect while session active: epoch=%" PRIu32 " conn=%u state=%s session=%" PRIu32,
            epoch,
            conn_handle,
            ble_audio_stream_transport_state_name(s_transport_state),
            s_transport_session_id);
    } else {
        ble_audio_stream_set_transport_state(
            BLE_AUDIO_STREAM_TRANSPORT_STATE_CONNECTED,
            "gap_connect");
    }

    if (s_pending_mtu_valid && s_pending_mtu_conn_handle == conn_handle &&
        s_pending_mtu_epoch == epoch) {
        ble_audio_stream_apply_mtu_value(conn_handle, s_pending_mtu_value);
        s_pending_mtu_valid = false;
        s_pending_mtu_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_mtu_epoch = 0;
        s_pending_mtu_value = 0;
    } else if (s_pending_mtu_valid) {
        ble_audio_stream_note_stale_event("mtu", s_pending_mtu_conn_handle, s_pending_mtu_epoch);
        s_pending_mtu_valid = false;
        s_pending_mtu_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_mtu_epoch = 0;
        s_pending_mtu_value = 0;
    }

    if (s_pending_subscribe_valid && s_pending_subscribe_conn_handle == conn_handle &&
        s_pending_subscribe_epoch == epoch) {
        ble_audio_stream_apply_notify_enabled(s_pending_notify_enabled);
        ESP_LOGI(
            TAG,
            "audio notify subscription restored before connect: epoch=%" PRIu32 " conn=%d notify=%u window=%u",
            epoch,
            conn_handle,
            s_pending_notify_enabled ? 1u : 0u,
            s_notify_window_depth);
        if (s_pending_notify_enabled) {
            ble_audio_stream_log_packet_size(conn_handle);
        }
        s_pending_subscribe_valid = false;
        s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_subscribe_epoch = 0;
    } else if (s_pending_subscribe_valid) {
        ble_audio_stream_note_stale_event(
            "subscribe",
            s_pending_subscribe_conn_handle,
            s_pending_subscribe_epoch);
        s_pending_subscribe_valid = false;
        s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_subscribe_epoch = 0;
        s_pending_notify_enabled = false;
    }

    ble_audio_stream_refresh_link_state("gap_connect_ready_check");
}

void ble_audio_stream_on_gap_disconnect(uint16_t conn_handle)
{
    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    if (link.conn_handle != conn_handle) {
        ble_audio_stream_note_stale_event("disconnect", conn_handle, link.connection_epoch);
        return;
    }

    uint32_t epoch = ble_audio_stream_advance_connection_epoch("gap_disconnect");
    if (ble_audio_stream_transport_session_active()) {
        ESP_LOGW(
            TAG,
            "audio transport link suspended: reason=disconnect epoch=%" PRIu32 " conn=%u state=%s session=%" PRIu32,
            epoch,
            conn_handle,
            ble_audio_stream_transport_state_name(s_transport_state),
            s_transport_session_id);
        ble_audio_stream_replay_mark_link_suspended("disconnect");
    }
    portENTER_CRITICAL(&s_link_state_lock);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_mtu_ready = false;
    s_notify_enabled = false;
    s_packet_value_max_bytes = BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES;
    portEXIT_CRITICAL(&s_link_state_lock);
    ble_audio_stream_reset_notify_credit();

    if (s_pending_subscribe_valid && s_pending_subscribe_conn_handle == conn_handle) {
        s_pending_subscribe_valid = false;
        s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_subscribe_epoch = 0;
        s_pending_notify_enabled = false;
    }

    if (s_pending_mtu_valid && s_pending_mtu_conn_handle == conn_handle) {
        s_pending_mtu_valid = false;
        s_pending_mtu_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_mtu_epoch = 0;
        s_pending_mtu_value = 0;
    }

    ble_audio_stream_refresh_link_state("gap_disconnect");
}

void ble_audio_stream_on_gap_subscribe(
    uint16_t conn_handle,
    uint16_t attr_handle,
    uint8_t cur_notify,
    uint8_t cur_indicate)
{
    (void)cur_indicate;
    if (attr_handle != s_notify_attr_handle) {
        return;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    if (conn_handle != link.conn_handle) {
        if (link.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_audio_stream_note_stale_event("subscribe", conn_handle, link.connection_epoch);
            return;
        }
        s_pending_subscribe_valid = true;
        s_pending_subscribe_conn_handle = conn_handle;
        s_pending_subscribe_epoch = link.connection_epoch + 1U;
        if (s_pending_subscribe_epoch == 0) {
            s_pending_subscribe_epoch = 1U;
        }
        s_pending_notify_enabled = cur_notify != 0;
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_STATE, DIAG_SEV_INFO,
                 0,
                 s_pending_subscribe_epoch,
                 conn_handle,
                 cur_notify != 0 ? BLE_AUDIO_NOTIFY_STATE_DEFERRED_ENABLED : BLE_AUDIO_NOTIFY_STATE_DEFERRED_DISABLED);
        ESP_LOGI(
            TAG,
            "audio notify subscription deferred until connect: pending_epoch=%" PRIu32 " conn=%d attr=%d notify=%u",
            s_pending_subscribe_epoch,
            conn_handle,
            attr_handle,
            cur_notify);
        return;
    }

    if (cur_notify == 0 && ble_audio_stream_transport_session_active()) {
        /* Keep the session active so queued stop/cancel can use the bounded
         * link recovery window instead of turning a cleanup toggle into start. */
        ESP_LOGW(
            TAG,
            "audio transport link suspended: reason=notify_disabled epoch=%" PRIu32 " conn=%u state=%s session=%" PRIu32,
            link.connection_epoch,
            conn_handle,
            ble_audio_stream_transport_state_name(s_transport_state),
            s_transport_session_id);
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_NOTIFY_STATE, DIAG_SEV_WARN,
                 s_transport_session_id,
                 link.connection_epoch,
                 conn_handle,
                 BLE_AUDIO_NOTIFY_STATE_DISABLED_ABORT);
        ble_audio_stream_replay_mark_link_suspended("notify_disabled");
    }

    s_pending_subscribe_valid = false;
    s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_pending_subscribe_epoch = 0;
    s_pending_notify_enabled = false;
    ble_audio_stream_apply_notify_enabled(cur_notify != 0);
    ble_audio_stream_log_notify_state(
        cur_notify != 0 ? BLE_AUDIO_NOTIFY_STATE_ENABLED : BLE_AUDIO_NOTIFY_STATE_DISABLED,
        conn_handle,
        DIAG_SEV_INFO);
    ESP_LOGI(
        TAG,
        "audio notify subscription changed: epoch=%" PRIu32 " conn=%d attr=%d notify=%u window=%u",
        link.connection_epoch,
        conn_handle,
        attr_handle,
        cur_notify,
        s_notify_window_depth);
    if (cur_notify != 0) {
        ble_audio_stream_log_packet_size(conn_handle);
    }
    ble_audio_stream_refresh_link_state(cur_notify != 0 ? "gap_subscribe_notify_on" : "gap_subscribe_notify_off");
}

void ble_audio_stream_on_gap_mtu(uint16_t conn_handle, uint16_t mtu)
{
    if (mtu <= 3) {
        return;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    if (conn_handle != link.conn_handle) {
        if (link.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_audio_stream_note_stale_event("mtu", conn_handle, link.connection_epoch);
            return;
        }
        s_pending_mtu_valid = true;
        s_pending_mtu_conn_handle = conn_handle;
        s_pending_mtu_epoch = link.connection_epoch + 1U;
        if (s_pending_mtu_epoch == 0) {
            s_pending_mtu_epoch = 1U;
        }
        s_pending_mtu_value = mtu;
        ESP_LOGI(
            TAG,
            "audio notify mtu deferred until connect: pending_epoch=%" PRIu32 " conn=%u mtu=%u",
            s_pending_mtu_epoch,
            conn_handle,
            mtu);
        return;
    }

    ble_audio_stream_apply_mtu_value(conn_handle, mtu);
    ble_audio_stream_refresh_link_state("gap_mtu");
}

void ble_audio_stream_on_gap_notify_tx(
    uint16_t conn_handle,
    uint16_t attr_handle,
    int status,
    bool indication)
{
    (void)indication;

    if (attr_handle != s_notify_attr_handle) {
        return;
    }

    ble_audio_stream_link_snapshot_t link = ble_audio_stream_get_link_snapshot();
    if (conn_handle != link.conn_handle || !ble_audio_stream_transport_link_ready()) {
        ble_audio_stream_note_stale_event("notify_tx", conn_handle, link.connection_epoch);
        return;
    }

    if (!s_notify_tx_wait_active || s_notify_tx_wait_epoch != link.connection_epoch) {
        ble_audio_stream_note_stale_event("notify_tx", conn_handle, s_notify_tx_wait_epoch);
        return;
    }

    if (status == 0 || status == BLE_HS_EDONE) {
        s_last_notify_tx_status = status;
        if (s_notify_tx_done_sem != NULL) {
            xSemaphoreGive(s_notify_tx_done_sem);
        }
        if (s_notify_credit_sem != NULL) {
            xSemaphoreGive(s_notify_credit_sem);
        }
        return;
    }

    if (status != BLE_HS_ENOMEM) {
        ESP_LOGW(
            TAG,
            "notify tx completion error: conn=%u attr=%u status=%d",
            conn_handle,
            attr_handle,
            status);
    }
    s_last_notify_tx_status = status;
    if (s_notify_tx_done_sem != NULL) {
        xSemaphoreGive(s_notify_tx_done_sem);
    }
    if (s_notify_credit_sem != NULL) {
        xSemaphoreGive(s_notify_credit_sem);
    }
}

bool ble_audio_stream_is_ready(void)
{
    return ble_audio_stream_transport_link_ready() &&
           s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY;
}

bool ble_audio_stream_is_busy(void)
{
    return s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING ||
           s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING ||
           (s_export_queue != NULL && uxQueueMessagesWaiting(s_export_queue) > 0);
}

void ble_audio_stream_get_backpressure(ble_audio_stream_backpressure_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->queue_capacity = BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH;
    snapshot->audio_pool_capacity = BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH;
    snapshot->transport_session_active = ble_audio_stream_transport_session_active();
    snapshot->link_ready = ble_audio_stream_transport_link_ready();

    if (s_export_queue != NULL) {
        snapshot->queue_depth = (uint32_t)uxQueueMessagesWaiting(s_export_queue);
    }

    if (s_audio_pool_mutex != NULL &&
        xSemaphoreTake(s_audio_pool_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        snapshot->audio_pool_in_use = s_audio_pool_in_use;
        snapshot->audio_pool_high_water = s_audio_pool_global_high_water;
        xSemaphoreGive(s_audio_pool_mutex);
    } else {
        snapshot->audio_pool_in_use = s_audio_pool_in_use;
        snapshot->audio_pool_high_water = s_audio_pool_global_high_water;
    }

    snapshot->pressure_percent = ble_audio_stream_pressure_percent(
        snapshot->queue_depth,
        snapshot->audio_pool_in_use);

    bool next_backpressure_active = ble_audio_stream_get_backpressure_active();
    if (!snapshot->transport_session_active) {
        next_backpressure_active = false;
    } else if (snapshot->pressure_percent >= BLE_AUDIO_STREAM_BACKPRESSURE_PAUSE_PERCENT) {
        next_backpressure_active = true;
    } else if (snapshot->pressure_percent <= BLE_AUDIO_STREAM_BACKPRESSURE_RESUME_PERCENT) {
        next_backpressure_active = false;
    }

    snapshot->pause_recommended = next_backpressure_active;
    snapshot->resume_recommended = !next_backpressure_active;

    if (ble_audio_stream_set_backpressure_active(next_backpressure_active)) {
        ESP_LOGI(
            TAG,
            "audio backpressure %s: session=%" PRIu32 " queued=%" PRIu32 "/%u pool=%" PRIu32 "/%u pressure=%" PRIu32 "%% link=%u",
            next_backpressure_active ? "pause" : "resume",
            s_transport_session_id,
            snapshot->queue_depth,
            (unsigned)BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH,
            snapshot->audio_pool_in_use,
            (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH,
            snapshot->pressure_percent,
            snapshot->link_ready ? 1u : 0u);
        ble_audio_stream_log_watermark(
            s_transport_session_id,
            snapshot->queue_depth,
            snapshot->audio_pool_in_use,
            snapshot->pressure_percent);
        diag_log(DIAG_SRC_BLE_AUDIO, DIAG_BAUD_BACKPRESSURE,
                 next_backpressure_active ? DIAG_SEV_WARN : DIAG_SEV_INFO,
                 s_transport_session_id,
                 next_backpressure_active ? 1U : 2U,
                 snapshot->queue_depth,
                 snapshot->audio_pool_in_use);
    }
}

uint16_t ble_audio_stream_get_notify_attr_handle(void)
{
    return s_notify_attr_handle;
}

esp_err_t ble_audio_stream_send_session_start(uint32_t session_id)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ble_audio_stream_transport_link_ready() ||
        s_transport_state != BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t payload_bytes = ble_audio_stream_get_audio_payload_bytes();
    if (payload_bytes == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,
        .session_id = session_id,
    };

    s_transport_session_id = session_id;
    s_transport_expected_packet_count = 0;
    s_transport_audio_payload_bytes = payload_bytes;
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING,
        "session_start_queued");
    ble_audio_stream_stats_begin(session_id);
    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        memset(&s_session_stats, 0, sizeof(s_session_stats));
        ble_audio_stream_reset_transport_session();
        ble_audio_stream_refresh_link_state("session_start_enqueue_failed");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_audio(
    uint32_t session_id,
    uint16_t packet_sequence,
    const uint8_t *pcm_buffer,
    uint16_t pcm_bytes)
{
    uint16_t packet_count = ble_audio_stream_count_audio_packets(pcm_bytes);
    if (!s_started || s_export_queue == NULL || pcm_buffer == NULL || pcm_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_transport_state != BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAMING ||
        s_transport_session_id != session_id) {
        ble_audio_stream_purge_queued_session_jobs(session_id, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (packet_count == 0 || (uint32_t)packet_sequence + packet_count > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA,
        .session_id = session_id,
        .packet_sequence = packet_sequence,
        .pcm_bytes = pcm_bytes,
    };
    esp_err_t pool_ret = ble_audio_stream_audio_pool_acquire(
        session_id,
        pcm_bytes,
        &job.owned_pcm);
    if (pool_ret != ESP_OK) {
        return pool_ret;
    }
    memcpy(job.owned_pcm, pcm_buffer, pcm_bytes);

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(BLE_AUDIO_STREAM_AUDIO_QUEUE_WAIT_MS)) != pdTRUE) {
        ble_audio_stream_stats_drop(session_id, "queue_full", ESP_ERR_TIMEOUT);
        ESP_LOGW(
            TAG,
            "audio data enqueue failed: session=%" PRIu32 " seq=%u pcm_bytes=%u queued=%" PRIu32 "/%" PRIu32 " pool_in_use=%" PRIu32 "/%u",
            session_id,
            packet_sequence,
            pcm_bytes,
            s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
            (uint32_t)BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH,
            s_audio_pool_in_use,
            (unsigned)BLE_AUDIO_STREAM_AUDIO_POOL_LENGTH);
        ble_audio_stream_free_job(&job);
        return ESP_ERR_TIMEOUT;
    }
    ble_audio_stream_note_transport_progress(
        session_id,
        (uint16_t)(packet_sequence + packet_count));
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_stop(uint32_t session_id, uint16_t expected_packet_count)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    bool link_ready = ble_audio_stream_transport_link_ready();
    bool active_session =
        ble_audio_stream_transport_session_active() &&
        s_transport_session_id == session_id;
    bool idle_ready =
        link_ready &&
        s_transport_state == BLE_AUDIO_STREAM_TRANSPORT_STATE_STREAM_READY;
    if (!active_session && !idle_ready) {
        ble_audio_stream_purge_queued_session_jobs(session_id, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (active_session && !link_ready) {
        ESP_LOGW(
            TAG,
            "audio session stop queued during link recovery: session=%" PRIu32 " expected_packet_count=%u queued=%" PRIu32 "/%" PRIu32,
            session_id,
            expected_packet_count,
            s_export_queue != NULL ? (uint32_t)uxQueueMessagesWaiting(s_export_queue) : 0,
            (uint32_t)BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH);
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
    };
    ble_audio_stream_job_t finalize_job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_FINALIZE_STOP,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
    };

    ble_audio_stream_transport_state_t previous_state = s_transport_state;
    ble_audio_stream_note_transport_progress(session_id, expected_packet_count);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
        "session_stop_queued");
    /* Keep STOP after accepted audio data so the receiver can treat it as the
     * ASR input boundary instead of a marker that still permits tail audio. */
    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ble_audio_stream_set_transport_state(
            previous_state,
            "session_stop_enqueue_failed");
        return ESP_ERR_TIMEOUT;
    }
    if (xQueueSend(s_export_queue, &finalize_job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ble_audio_stream_set_transport_state(
            previous_state,
            "session_stop_finalize_enqueue_failed");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_error(
    uint32_t session_id,
    uint16_t expected_packet_count,
    uint16_t error_code)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_purge_queued_session_jobs(session_id, true);
    if (!ble_audio_stream_transport_session_active() ||
        s_transport_session_id != session_id) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_ERROR,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
        .error_code = error_code,
    };

    ble_audio_stream_transport_state_t previous_state = s_transport_state;
    ble_audio_stream_note_transport_progress(session_id, expected_packet_count);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
        "session_error_queued");
    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ble_audio_stream_set_transport_state(
            previous_state,
            "session_error_enqueue_failed");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_cancel(uint32_t session_id, uint16_t expected_packet_count)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_purge_queued_session_jobs(session_id, true);
    if (!ble_audio_stream_transport_session_active() ||
        s_transport_session_id != session_id) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
    };

    ble_audio_stream_transport_state_t previous_state = s_transport_state;
    ble_audio_stream_note_transport_progress(session_id, expected_packet_count);
    ble_audio_stream_set_transport_state(
        BLE_AUDIO_STREAM_TRANSPORT_STATE_DRAINING,
        "session_cancel_queued");
    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ble_audio_stream_set_transport_state(
            previous_state,
            "session_cancel_enqueue_failed");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
