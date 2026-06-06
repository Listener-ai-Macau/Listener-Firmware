#include "audio_capture.h"
#include "audio_capture_platform.h"
#include "ble_audio_stream.h"
#include "board_pins.h"
#include "listener_audio_proto.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
#include "driver/i2c_master.h"
#endif

#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_check.h"

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#endif

#include "esp_log.h"
#include "diag_log.h"
#include "diag_log_events.h"
#include "watchdog_platform.h"

/* ---------- Shared defines ---------- */

#define AUDIO_CAPTURE_SAMPLE_RATE_HZ    (16000)
#define AUDIO_CAPTURE_FRAME_MS          (20)
#define AUDIO_CAPTURE_FRAME_SAMPLES     ((AUDIO_CAPTURE_SAMPLE_RATE_HZ * AUDIO_CAPTURE_FRAME_MS) / 1000)
#define AUDIO_CAPTURE_FRAME_BYTES       (AUDIO_CAPTURE_FRAME_SAMPLES * sizeof(int16_t))
#define AUDIO_CAPTURE_LOG_INTERVAL_FRAMES (50)
#define AUDIO_CAPTURE_TASK_STACK_BYTES  (6 * 1024)
#define AUDIO_CAPTURE_V2_MIC_BLOCKER "V2 CLK/GPIO48 DOUT/GPIO47 microphone interface not validated"
/* Recording duration is user-controlled (KEY1 toggle); no fixed upper limit.
 * The only hard limit is uint16_t packet_sequence overflow in the BLE protocol,
 * which is handled gracefully by sending session_stop before overflow. */
#define AUDIO_CAPTURE_STREAM_BATCH_FRAMES 3
#define AUDIO_CAPTURE_STREAM_BATCH_BYTES (AUDIO_CAPTURE_STREAM_BATCH_FRAMES * AUDIO_CAPTURE_FRAME_BYTES)
#define AUDIO_CAPTURE_STREAM_PROGRESS_LOG_PACKET_INTERVAL 64U
#define AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS AUDIO_CAPTURE_FRAME_MS
#define AUDIO_CAPTURE_BACKPRESSURE_LOG_INTERVAL_FRAMES 50U

/* ---------- ES8311-specific defines ---------- */

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
#define AUDIO_CAPTURE_I2C_PORT          (0)
#define AUDIO_CAPTURE_I2C_SDA_IO        (4)
#define AUDIO_CAPTURE_I2C_SCL_IO        (5)
#define AUDIO_CAPTURE_ES8311_I2S_PORT   (0)
#define AUDIO_CAPTURE_ES8311_I2S_MCLK_IO (GPIO_NUM_45)
#define AUDIO_CAPTURE_ES8311_I2S_BCLK_IO (GPIO_NUM_39)
#define AUDIO_CAPTURE_ES8311_I2S_WS_IO  (GPIO_NUM_41)
#define AUDIO_CAPTURE_ES8311_I2S_DIN_IO (GPIO_NUM_40)
#define AUDIO_CAPTURE_ES8311_I2S_DOUT_IO (GPIO_NUM_NC)
#define AUDIO_CAPTURE_MCLK_MULTIPLE     (384)
#define AUDIO_CAPTURE_INPUT_GAIN_DB     (42.0f)
#endif

/* ---------- SPH0655-specific defines ---------- */

#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#include "driver/i2s_pdm.h"
#endif

static const char *TAG = "audio_capture";

/* ---------- Types ---------- */

typedef enum {
    AUDIO_CAPTURE_EXPORT_MODE_NONE = 0,
    AUDIO_CAPTURE_EXPORT_MODE_SESSION,
} audio_capture_export_mode_t;

typedef struct {
    audio_capture_export_mode_t mode;
    bool requested;
    bool active;
    bool ble_session_started;
    bool stop_requested;
    bool cancel_requested;
    uint32_t duration_seconds;
    uint32_t total_frames;
    uint32_t captured_frames;
    size_t pcm_bytes_total;
    size_t pcm_bytes_written;
    uint32_t session_id;
    uint16_t stream_next_packet_sequence;
    uint16_t stream_batch_frame_count;
    uint8_t stream_batch_buffer[AUDIO_CAPTURE_STREAM_BATCH_BYTES];
    uint8_t stream_emit_buffer[AUDIO_CAPTURE_STREAM_BATCH_BYTES];
} audio_capture_export_state_t;

/* ---------- Shared state ---------- */

static bool s_started;
static i2s_chan_handle_t s_i2s_rx_handle;

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
static i2c_master_bus_handle_t s_i2c_bus_handle;
static esp_codec_dev_handle_t s_codec_handle;
#endif

static TaskHandle_t s_capture_task_handle;
static volatile bool s_idle_power_save_requested;
static bool s_i2s_low_power_disabled;
static uint32_t s_frame_count;
static uint32_t s_frame_captured_count;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
static uint32_t s_overflow_count;
static uint32_t s_underrun_count;
#endif
static uint32_t s_dropped_frame_count;
static audio_capture_export_state_t s_export_state;
static SemaphoreHandle_t s_state_mutex;
static uint32_t s_session_id_counter;
static bool s_capture_backpressure_paused;
static uint32_t s_capture_backpressure_frames;

static const char *audio_capture_static_unavailable_reason(void)
{
#if defined(CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8) && CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8 && \
    defined(CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM) && CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM && \
    !CONFIG_AUDIO_CAPTURE_V2_MIC_INTERFACE_VALIDATED
    return AUDIO_CAPTURE_V2_MIC_BLOCKER;
#else
    return NULL;
#endif
}

/* ---------- Shared helpers ---------- */

static void audio_capture_export_cleanup(void)
{
    memset(&s_export_state, 0, sizeof(s_export_state));
    s_capture_backpressure_paused = false;
    s_capture_backpressure_frames = 0;
}

static bool audio_capture_backpressure_should_pause(void)
{
    ble_audio_stream_backpressure_t pressure = {0};
    ble_audio_stream_get_backpressure(&pressure);

    bool stop_or_cancel_requested = false;
    if (s_state_mutex != NULL &&
        xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        stop_or_cancel_requested = s_export_state.stop_requested || s_export_state.cancel_requested;
        xSemaphoreGive(s_state_mutex);
    }

    if (!pressure.transport_session_active) {
        if (s_capture_backpressure_paused) {
            s_capture_backpressure_paused = false;
        }
        return false;
    }

    bool should_pause = pressure.pause_recommended && !stop_or_cancel_requested;
    if (should_pause) {
        s_capture_backpressure_frames++;
    }

    if (should_pause != s_capture_backpressure_paused) {
        s_capture_backpressure_paused = should_pause;
        uint32_t session_id = 0;
        if (s_state_mutex != NULL &&
            xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            session_id = s_export_state.session_id;
            xSemaphoreGive(s_state_mutex);
        }
        ESP_LOGI(
            TAG,
            "record capture backpressure %s: session=%" PRIu32 " queued=%" PRIu32 "/%" PRIu32 " pool=%" PRIu32 "/%" PRIu32 " pressure=%" PRIu32 "%% paused_frames=%" PRIu32,
            should_pause ? "pause" : "resume",
            session_id,
            pressure.queue_depth,
            pressure.queue_capacity,
            pressure.audio_pool_in_use,
            pressure.audio_pool_capacity,
            pressure.pressure_percent,
            s_capture_backpressure_frames);
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_BACKPRESSURE,
                 should_pause ? DIAG_SEV_WARN : DIAG_SEV_INFO,
                 session_id,
                 should_pause ? 1U : 2U,
                 pressure.queue_depth,
                 pressure.audio_pool_in_use);
    } else if (should_pause &&
               (s_capture_backpressure_frames % AUDIO_CAPTURE_BACKPRESSURE_LOG_INTERVAL_FRAMES) == 0) {
        ESP_LOGW(
            TAG,
            "record capture still paused by BLE backpressure: queued=%" PRIu32 "/%" PRIu32 " pool=%" PRIu32 "/%" PRIu32 " pressure=%" PRIu32 "%% paused_frames=%" PRIu32,
            pressure.queue_depth,
            pressure.queue_capacity,
            pressure.audio_pool_in_use,
            pressure.audio_pool_capacity,
            pressure.pressure_percent,
            s_capture_backpressure_frames);
    }

    return should_pause;
}

static uint32_t audio_capture_packet_safe_total_frames(uint16_t payload_bytes)
{
    if (payload_bytes == 0) {
        return 0;
    }

    uint64_t max_pcm_bytes = (uint64_t)UINT16_MAX * (uint64_t)payload_bytes;
    uint64_t max_frames = max_pcm_bytes / AUDIO_CAPTURE_FRAME_BYTES;
    if (max_frames > UINT32_MAX) {
        return UINT32_MAX;
    }

    return (uint32_t)max_frames;
}

static bool audio_capture_packet_sequence_can_advance(uint16_t next_packet_sequence, uint16_t packet_count)
{
    return packet_count > 0 && (uint32_t)next_packet_sequence + packet_count <= UINT16_MAX;
}

static uint16_t audio_capture_session_error_from_stream_result(esp_err_t result)
{
    switch (result) {
        case ESP_ERR_TIMEOUT:
            return LISTENER_AUDIO_SESSION_ERROR_QUEUE_FULL;
        case ESP_ERR_INVALID_STATE:
            return LISTENER_AUDIO_SESSION_ERROR_LINK_LOST;
        case ESP_ERR_NO_MEM:
            return LISTENER_AUDIO_SESSION_ERROR_NO_MEMORY;
        case ESP_ERR_INVALID_SIZE:
            return LISTENER_AUDIO_SESSION_ERROR_PACKET_TOO_LARGE;
        default:
            break;
    }

    return LISTENER_AUDIO_SESSION_ERROR_TRANSPORT;
}

static esp_err_t audio_capture_apply_idle_power_save(bool enabled)
{
    if (!s_started || s_i2s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled == s_i2s_low_power_disabled) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (enabled) {
#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
        if (s_codec_handle != NULL) {
            int mute_ret = esp_codec_dev_set_in_mute(s_codec_handle, true);
            if (mute_ret != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "codec input mute failed before idle power save: %d", mute_ret);
            }
        }
#endif
        ret = i2s_channel_disable(s_i2s_rx_handle);
        if (ret == ESP_OK) {
            s_i2s_low_power_disabled = true;
        }
    } else {
        ret = i2s_channel_enable(s_i2s_rx_handle);
        if (ret == ESP_OK) {
            s_i2s_low_power_disabled = false;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
            if (s_codec_handle != NULL) {
                int mute_ret = esp_codec_dev_set_in_mute(s_codec_handle, false);
                if (mute_ret != ESP_CODEC_DEV_OK) {
                    ESP_LOGW(TAG, "codec input unmute failed after idle power save: %d", mute_ret);
                }
            }
#endif
        }
    }

    ESP_LOGI(
        TAG,
        "audio idle power save %s: %s",
        enabled ? "enabled" : "disabled",
        esp_err_to_name(ret));
    diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_IDLE_POWER,
             ret == ESP_OK ? DIAG_SEV_INFO : DIAG_SEV_WARN,
             enabled ? 1 : 0, ret, 0, 0);
    return ret;
}

/* ---------- Session API (shared) ---------- */

esp_err_t audio_capture_session_begin(void)
{
    const char *unavailable_reason = audio_capture_static_unavailable_reason();
    if (unavailable_reason != NULL) {
        ESP_LOGW(TAG, "record session start rejected: audio capture unavailable: %s", unavailable_reason);
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION_REJ, DIAG_SEV_WARN,
                 3, ESP_ERR_NOT_SUPPORTED, 0, 0);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!s_started) {
        ESP_LOGW(TAG, "record session start rejected: audio capture not started");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION_REJ, DIAG_SEV_WARN,
                 4, ESP_ERR_INVALID_STATE, 0, 0);
        return ESP_ERR_INVALID_STATE;
    }

    s_idle_power_save_requested = false;
    (void)audio_capture_apply_idle_power_save(false);

    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_export_state.requested || s_export_state.active) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (!ble_audio_stream_is_ready()) {
        xSemaphoreGive(s_state_mutex);
        ESP_LOGW(TAG, "record session start rejected: BLE audio transport not ready");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION_REJ, DIAG_SEV_WARN, 1, 0, 0, 0);
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t payload_bytes = ble_audio_stream_get_audio_payload_bytes();
    uint32_t packet_safe_total_frames = audio_capture_packet_safe_total_frames(payload_bytes);
    if (packet_safe_total_frames == 0) {
        xSemaphoreGive(s_state_mutex);
        ESP_LOGW(TAG, "record session start rejected: audio payload unavailable");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION_REJ, DIAG_SEV_WARN, 2, 0, 0, 0);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Use packet_safe_total_frames as the only limit (protocol-level packet
     * sequence overflow protection). If payload is unavailable, this will be 0
     * and we reject; otherwise recording continues until user stops or protocol
     * limit is approached. */
    uint32_t total_frames = packet_safe_total_frames;

    memset(&s_export_state, 0, sizeof(s_export_state));
    s_export_state.mode = AUDIO_CAPTURE_EXPORT_MODE_SESSION;
    s_export_state.duration_seconds = 0;
    s_export_state.total_frames = total_frames;
    s_export_state.pcm_bytes_total = 0;
    s_export_state.session_id = ++s_session_id_counter;
    uint32_t session_id = s_export_state.session_id;
    uint32_t session_max_seconds = (s_export_state.total_frames * AUDIO_CAPTURE_FRAME_MS) / 1000U;

    s_export_state.requested = true;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(
        TAG,
        "record session begin requested: session_id=%" PRIu32 " buffer_ms=%u buffer_bytes=%u packet_payload_bytes=%u packet_safe_max_s=%" PRIu32,
        session_id,
        AUDIO_CAPTURE_STREAM_BATCH_FRAMES * AUDIO_CAPTURE_FRAME_MS,
        (unsigned)AUDIO_CAPTURE_STREAM_BATCH_BYTES,
        payload_bytes,
        session_max_seconds);
    diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION, DIAG_SEV_INFO, 1, session_id, 0, 0);
    return ESP_OK;
}

esp_err_t audio_capture_session_stop(void)
{
    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!(s_export_state.requested || s_export_state.active) ||
        s_export_state.mode != AUDIO_CAPTURE_EXPORT_MODE_SESSION) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_export_state.stop_requested = true;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(TAG, "record session stop requested");
    return ESP_OK;
}

esp_err_t audio_capture_session_cancel(void)
{
    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!(s_export_state.requested || s_export_state.active) ||
        s_export_state.mode != AUDIO_CAPTURE_EXPORT_MODE_SESSION) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_export_state.requested && !s_export_state.active) {
        audio_capture_export_cleanup();
        xSemaphoreGive(s_state_mutex);
        ESP_LOGI(TAG, "record session canceled before activation");
        return ESP_OK;
    }

    s_export_state.cancel_requested = true;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(TAG, "record session cancel requested");
    return ESP_OK;
}

bool audio_capture_session_is_active(void)
{
    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool active = s_export_state.requested || s_export_state.active;
    xSemaphoreGive(s_state_mutex);
    return active;
}

esp_err_t audio_capture_set_idle_power_save(bool enabled)
{
    s_idle_power_save_requested = enabled;
    if (!enabled) {
        return audio_capture_apply_idle_power_save(false);
    }
    if (audio_capture_session_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* ---------- Shared frame processing ---------- */

static void audio_capture_process_frame(const int16_t *frame_buffer)
{
    s_frame_captured_count++;
    s_frame_count++;

    bool should_emit = false;
    bool should_cancel = false;
    bool stream_failed = false;
    bool should_transport_error = false;
    bool idle_for_logging = false;
    bool should_session_start = false;
    bool should_session_audio = false;
    bool should_session_stop = false;
    bool should_session_cancel = false;
    bool should_session_error = false;
    uint32_t session_id = 0;
    uint16_t packet_sequence_start = 0;
    uint16_t batch_pcm_bytes = 0;
    uint16_t expected_packet_count_at_end = 0;
    uint16_t session_error_code = LISTENER_AUDIO_SESSION_ERROR_NONE;
    uint16_t packet_count = 0;
    const uint8_t *audio_batch_copy = NULL;

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_export_state.requested && !s_export_state.active) {
            s_export_state.requested = false;
            s_export_state.active = true;
            s_export_state.captured_frames = 0;
            s_export_state.pcm_bytes_written = 0;
            s_export_state.stream_next_packet_sequence = 0;
            s_export_state.stream_batch_frame_count = 0;
            s_export_state.ble_session_started = false;
        }

        if (s_export_state.active && s_export_state.captured_frames < s_export_state.total_frames) {
            bool stop_boundary_requested =
                s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION &&
                s_export_state.stop_requested;
            bool hit_safety_max_duration = false;
            if (s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION) {
                if (!s_export_state.ble_session_started) {
                    should_session_start = true;
                    session_id = s_export_state.session_id;
                    s_export_state.ble_session_started = true;
                }

                /* Stop is a hard capture boundary; the frame that woke this
                 * call may already be after the user's stop edge. */
                if (!stop_boundary_requested) {
                    size_t batch_offset =
                        (size_t)s_export_state.stream_batch_frame_count * AUDIO_CAPTURE_FRAME_BYTES;
                    memcpy(s_export_state.stream_batch_buffer + batch_offset, frame_buffer, AUDIO_CAPTURE_FRAME_BYTES);
                    s_export_state.stream_batch_frame_count++;
                    s_export_state.pcm_bytes_written += AUDIO_CAPTURE_FRAME_BYTES;

                    if (s_export_state.stream_batch_frame_count >= AUDIO_CAPTURE_STREAM_BATCH_FRAMES) {
                        session_id = s_export_state.session_id;
                        packet_sequence_start = s_export_state.stream_next_packet_sequence;
                        batch_pcm_bytes = AUDIO_CAPTURE_STREAM_BATCH_BYTES;
                        memcpy(s_export_state.stream_emit_buffer, s_export_state.stream_batch_buffer, batch_pcm_bytes);
                        audio_batch_copy = s_export_state.stream_emit_buffer;
                        packet_count = ble_audio_stream_count_audio_packets(batch_pcm_bytes);
                        if (!audio_capture_packet_sequence_can_advance(
                                s_export_state.stream_next_packet_sequence,
                                packet_count)) {
                            /* Protocol packet sequence limit reached; perform graceful
                             * session_stop instead of error so the host receives all
                             * audio up to this point with a clean termination. */
                            should_session_stop = true;
                            session_id = s_export_state.session_id;
                            expected_packet_count_at_end = s_export_state.stream_next_packet_sequence;
                            should_emit = true;
                            ESP_LOGI(
                                TAG,
                                "record session stopping at protocol limit: session_id=%" PRIu32 " next=%u add=%u",
                                session_id,
                                s_export_state.stream_next_packet_sequence,
                                packet_count);
                        } else {
                            should_session_audio = true;
                            s_export_state.stream_next_packet_sequence =
                                (uint16_t)(s_export_state.stream_next_packet_sequence + packet_count);
                            s_export_state.stream_batch_frame_count = 0;
                        }
                    }
                }
            }

            if (!should_cancel && !stop_boundary_requested) {
                s_export_state.captured_frames++;
                hit_safety_max_duration = s_export_state.captured_frames >= s_export_state.total_frames;
            }

            if (!should_cancel && !should_session_stop &&
                s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION &&
                (stop_boundary_requested || hit_safety_max_duration)) {
                if (hit_safety_max_duration) {
                    ESP_LOGI(
                        TAG,
                        "record session stopping at protocol limit (total_frames): session_id=%" PRIu32 " captured_frames=%" PRIu32,
                        s_export_state.session_id,
                        s_export_state.captured_frames);
                }
                s_export_state.duration_seconds =
                    (s_export_state.captured_frames * AUDIO_CAPTURE_FRAME_MS) / 1000U;

                if (s_export_state.stream_batch_frame_count > 0) {
                    session_id = s_export_state.session_id;
                    packet_sequence_start = s_export_state.stream_next_packet_sequence;
                    batch_pcm_bytes =
                        s_export_state.stream_batch_frame_count * AUDIO_CAPTURE_FRAME_BYTES;
                    memcpy(s_export_state.stream_emit_buffer, s_export_state.stream_batch_buffer, batch_pcm_bytes);
                    audio_batch_copy = s_export_state.stream_emit_buffer;
                    packet_count = ble_audio_stream_count_audio_packets(batch_pcm_bytes);
                    if (!audio_capture_packet_sequence_can_advance(
                            s_export_state.stream_next_packet_sequence,
                            packet_count)) {
                        /* Protocol limit at final batch; stop gracefully with
                         * whatever audio we have so far. */
                        ESP_LOGI(
                            TAG,
                            "record session stopping at protocol limit (final batch): session_id=%" PRIu32 " next=%u add=%u",
                            session_id,
                            s_export_state.stream_next_packet_sequence,
                            packet_count);
                    } else {
                        should_session_audio = true;
                        s_export_state.stream_next_packet_sequence =
                            (uint16_t)(s_export_state.stream_next_packet_sequence + packet_count);
                        s_export_state.stream_batch_frame_count = 0;
                    }
                }
                if (!should_cancel) {
                    should_session_stop = true;
                    session_id = s_export_state.session_id;
                    expected_packet_count_at_end = s_export_state.stream_next_packet_sequence;
                    should_emit = true;
                }
            }
        }

        if (s_export_state.active && s_export_state.cancel_requested) {
            should_cancel = true;
            if (s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION) {
                should_session_cancel = s_export_state.ble_session_started;
                session_id = s_export_state.session_id;
                expected_packet_count_at_end = s_export_state.stream_next_packet_sequence;
            }
        }

        idle_for_logging = !s_export_state.active && !s_export_state.requested;
        xSemaphoreGive(s_state_mutex);
    }

    if (should_cancel) {
        if (should_session_error) {
            esp_err_t error_ret = ble_audio_stream_send_session_error(
                session_id,
                expected_packet_count_at_end,
                session_error_code);
            if (error_ret != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "stream session error failed: session_id=%" PRIu32 " expected_packet_count=%u error_code=%u ret=%s",
                    session_id,
                    expected_packet_count_at_end,
                    session_error_code,
                    esp_err_to_name(error_ret));
            }
        }
        if (should_session_cancel) {
            esp_err_t cancel_ret = ble_audio_stream_send_session_cancel(session_id, expected_packet_count_at_end);
            if (cancel_ret != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "stream session cancel failed: session_id=%" PRIu32 " expected_packet_count=%u ret=%s",
                    session_id,
                    expected_packet_count_at_end,
                    esp_err_to_name(cancel_ret));
            }
        }
        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            audio_capture_export_cleanup();
            xSemaphoreGive(s_state_mutex);
        }
        ESP_LOGI(TAG, "record session canceled");
    } else {
        if (should_session_start) {
            esp_err_t start_ret = ble_audio_stream_send_session_start(session_id);
            if (start_ret != ESP_OK) {
                stream_failed = true;
                should_transport_error = true;
                session_error_code = audio_capture_session_error_from_stream_result(start_ret);
                ESP_LOGW(TAG, "stream session start failed: session_id=%" PRIu32 " ret=%s", session_id, esp_err_to_name(start_ret));
            } else {
                ESP_LOGI(TAG, "stream session start queued: session_id=%" PRIu32, session_id);
            }
        }

        if (!stream_failed && should_session_audio) {
            esp_err_t audio_ret =
                ble_audio_stream_send_session_audio(session_id, packet_sequence_start, audio_batch_copy, batch_pcm_bytes);
            if (audio_ret != ESP_OK) {
                stream_failed = true;
                should_transport_error = true;
                session_error_code = audio_capture_session_error_from_stream_result(audio_ret);
                ESP_LOGW(
                    TAG,
                    "stream audio packet batch failed: session_id=%" PRIu32 " seq_start=%u packet_count=%u pcm_bytes=%u ret=%s",
                    session_id,
                    packet_sequence_start,
                    packet_count,
                    batch_pcm_bytes,
                    esp_err_to_name(audio_ret));
            } else if (packet_sequence_start == 0 ||
                       ((uint32_t)packet_sequence_start %
                        AUDIO_CAPTURE_STREAM_PROGRESS_LOG_PACKET_INTERVAL) == 0) {
                ESP_LOGI(
                    TAG,
                    "stream audio packet batch queued: session_id=%" PRIu32 " seq_start=%u packet_count=%u pcm_bytes=%u",
                    session_id,
                    packet_sequence_start,
                    packet_count,
                    batch_pcm_bytes);
            }
        }

        if (stream_failed) {
            if (should_transport_error) {
                esp_err_t error_ret = ble_audio_stream_send_session_error(
                    session_id,
                    packet_sequence_start,
                    session_error_code);
                if (error_ret != ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "stream session error after transport failure failed: session_id=%" PRIu32 " expected_packet_count=%u error_code=%u ret=%s",
                        session_id,
                        packet_sequence_start,
                        session_error_code,
                        esp_err_to_name(error_ret));
                }
            }
            if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
                audio_capture_export_cleanup();
                xSemaphoreGive(s_state_mutex);
            }
            ESP_LOGW(TAG, "record session aborted after BLE transport failure: session_id=%" PRIu32, session_id);
        } else if (should_session_stop) {
            esp_err_t stop_ret = ble_audio_stream_send_session_stop(session_id, expected_packet_count_at_end);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "stream session stop failed: session_id=%" PRIu32 " expected_packet_count=%u ret=%s",
                    session_id,
                    expected_packet_count_at_end,
                    esp_err_to_name(stop_ret));
            } else {
                ESP_LOGI(
                    TAG,
                    "stream session stop queued: session_id=%" PRIu32 " expected_packet_count=%u duration_s=%" PRIu32,
                    session_id,
                    expected_packet_count_at_end,
                    s_export_state.duration_seconds);
            }
        }

        if (!stream_failed && should_emit) {
            uint32_t stop_duration = 0;
            uint32_t stop_frames = 0;
            if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
                stop_duration = s_export_state.duration_seconds;
                stop_frames = s_export_state.captured_frames;
                audio_capture_export_cleanup();
                xSemaphoreGive(s_state_mutex);
            }
            diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_SESSION, DIAG_SEV_INFO,
                     2, session_id, stop_duration, stop_frames);
        }
    }
    if (idle_for_logging && (s_frame_captured_count % AUDIO_CAPTURE_LOG_INTERVAL_FRAMES) == 0) {
        ESP_LOGI(
            TAG,
            "frame captured count=%" PRIu32 " bytes=%" PRIu32,
            s_frame_captured_count,
            s_frame_captured_count * (uint32_t)AUDIO_CAPTURE_FRAME_BYTES);
    }
}

/* ========== ES8311 hardware path ========== */

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311

i2c_master_bus_handle_t audio_capture_get_i2c_bus_handle(void)
{
    return s_i2c_bus_handle;
}

static void audio_capture_task(void *arg)
{
    int16_t frame_buffer[AUDIO_CAPTURE_FRAME_SAMPLES];
    (void)watchdog_platform_subscribe_current_task("audio_capture_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (s_idle_power_save_requested && !audio_capture_session_is_active()) {
            (void)audio_capture_apply_idle_power_save(true);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        (void)audio_capture_apply_idle_power_save(false);

        if (audio_capture_backpressure_should_pause()) {
            vTaskDelay(pdMS_TO_TICKS(AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS));
            continue;
        }

        int ret = esp_codec_dev_read(s_codec_handle, frame_buffer, sizeof(frame_buffer));
        if (ret == ESP_CODEC_DEV_OK) {
            if (audio_capture_backpressure_should_pause()) {
                vTaskDelay(pdMS_TO_TICKS(AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS));
                continue;
            }
            audio_capture_process_frame(frame_buffer);
            continue;
        }

        if (ret == ESP_CODEC_DEV_WRONG_STATE) {
            s_underrun_count++;
            ESP_LOGW(TAG, "underrun count=%" PRIu32 " ret=%d", s_underrun_count, ret);
            diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_UNDERRUN, DIAG_SEV_WARN, s_underrun_count, 0, 0, 0);
        } else if (ret == ESP_CODEC_DEV_DRV_ERR || ret == ESP_CODEC_DEV_READ_FAIL) {
            s_overflow_count++;
            s_dropped_frame_count++;
            ESP_LOGW(
                TAG,
                "overflow count=%" PRIu32 " dropped frame=%" PRIu32 " ret=%d",
                s_overflow_count,
                s_dropped_frame_count,
                ret);
            diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_DROP, DIAG_SEV_WARN, s_dropped_frame_count, ret, 0, 0);
        } else {
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "dropped frame=%" PRIu32 " ret=%d", s_dropped_frame_count, ret);
            diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_DROP, DIAG_SEV_WARN, s_dropped_frame_count, ret, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t audio_capture_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_CAPTURE_ES8311_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx_handle), TAG, "create i2s channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = AUDIO_CAPTURE_ES8311_I2S_MCLK_IO,
            .bclk = AUDIO_CAPTURE_ES8311_I2S_BCLK_IO,
            .ws = AUDIO_CAPTURE_ES8311_I2S_WS_IO,
            .dout = AUDIO_CAPTURE_ES8311_I2S_DOUT_IO,
            .din = AUDIO_CAPTURE_ES8311_I2S_DIN_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = AUDIO_CAPTURE_MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg), TAG, "init i2s rx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx_handle), TAG, "enable i2s rx failed");
    ESP_LOGI(
        TAG,
        "ES8311 I2S init: %uHz mclk=%d bclk=%d ws=%d din=%d",
        AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        AUDIO_CAPTURE_ES8311_I2S_MCLK_IO,
        AUDIO_CAPTURE_ES8311_I2S_BCLK_IO,
        AUDIO_CAPTURE_ES8311_I2S_WS_IO,
        AUDIO_CAPTURE_ES8311_I2S_DIN_IO);
    return ESP_OK;
}

static esp_err_t audio_capture_i2c_init(void)
{
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = AUDIO_CAPTURE_I2C_PORT,
        .sda_io_num = AUDIO_CAPTURE_I2C_SDA_IO,
        .scl_io_num = AUDIO_CAPTURE_I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&i2c_mst_cfg, &s_i2c_bus_handle);
}

static esp_err_t audio_capture_codec_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = AUDIO_CAPTURE_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_FAIL, TAG, "create codec i2c ctrl failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = AUDIO_CAPTURE_ES8311_I2S_PORT,
        .rx_handle = s_i2s_rx_handle,
        .tx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "create codec i2s data failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_FAIL, TAG, "create codec gpio failed");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = -1,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = AUDIO_CAPTURE_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(es8311_if != NULL, ESP_FAIL, TAG, "create es8311 interface failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    s_codec_handle = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec_handle != NULL, ESP_FAIL, TAG, "create codec handle failed");

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 1,
        .channel_mask = 0x01,
        .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        .mclk_multiple = AUDIO_CAPTURE_MCLK_MULTIPLE,
    };
    ESP_RETURN_ON_FALSE(
        esp_codec_dev_open(s_codec_handle, &sample_cfg) == ESP_CODEC_DEV_OK,
        ESP_FAIL,
        TAG,
        "open codec device failed");
    ESP_RETURN_ON_FALSE(
        esp_codec_dev_set_in_gain(s_codec_handle, AUDIO_CAPTURE_INPUT_GAIN_DB) == ESP_CODEC_DEV_OK,
        ESP_FAIL,
        TAG,
        "set input gain failed");
    ESP_LOGI(TAG, "input gain set to %.1f dB", AUDIO_CAPTURE_INPUT_GAIN_DB);
    return ESP_OK;
}

#endif /* CONFIG_AUDIO_CAPTURE_MIC_ES8311 */

/* ========== SPH0655 PDM hardware path ========== */

#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM

static void audio_capture_task(void *arg)
{
    int16_t frame_buffer[AUDIO_CAPTURE_FRAME_SAMPLES];
    (void)watchdog_platform_subscribe_current_task("audio_capture_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (s_idle_power_save_requested && !audio_capture_session_is_active()) {
            (void)audio_capture_apply_idle_power_save(true);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        (void)audio_capture_apply_idle_power_save(false);

        if (audio_capture_backpressure_should_pause()) {
            vTaskDelay(pdMS_TO_TICKS(AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS));
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(
            s_i2s_rx_handle, frame_buffer, sizeof(frame_buffer),
            &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK && bytes_read == sizeof(frame_buffer)) {
            if (audio_capture_backpressure_should_pause()) {
                vTaskDelay(pdMS_TO_TICKS(AUDIO_CAPTURE_BACKPRESSURE_PAUSE_MS));
                continue;
            }
            audio_capture_process_frame(frame_buffer);
            continue;
        }

        if (ret != ESP_OK) {
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "pdm read failed: dropped=%" PRIu32 " ret=%s",
                     s_dropped_frame_count, esp_err_to_name(ret));
            diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_I2S_FAIL, DIAG_SEV_WARN, s_dropped_frame_count, ret, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t audio_capture_i2s_init(void)
{
#if !SOC_I2S_SUPPORTS_PDM2PCM
    ESP_LOGE(TAG, "SPH0655 PDM microphone requires hardware PDM2PCM support");
    return ESP_ERR_NOT_SUPPORTED;
#else
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_PINS_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx_handle), TAG, "create i2s channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = BOARD_PINS_MIC_CLK_IO,
            .din = BOARD_PINS_MIC_DOUT_IO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
#if defined(SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER) && SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_cfg.slot_cfg.hp_en = true;
    pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 35.5f;
    pdm_cfg.slot_cfg.amplify_num = 1;
#endif

    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_i2s_rx_handle, &pdm_cfg), TAG, "init pdm rx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx_handle), TAG, "enable pdm rx failed");
    ESP_LOGI(
        TAG,
        "SPH0655 PDM mic init: %uHz 16-bit mono clk=%d din=%d hp_filter=%u",
        AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        (int)BOARD_PINS_MIC_CLK_IO,
        (int)BOARD_PINS_MIC_DOUT_IO,
#if defined(SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER) && SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
        1u
#else
        0u
#endif
    );
    return ESP_OK;
#endif
}

#endif /* CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM */

/* ========== Public API ========== */

esp_err_t audio_capture_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    const char *unavailable_reason = audio_capture_static_unavailable_reason();
    if (unavailable_reason != NULL) {
        ESP_LOGW(TAG, "audio capture degraded: %s", unavailable_reason);
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_INIT_FAIL, DIAG_SEV_WARN, 8,
                 ESP_ERR_NOT_SUPPORTED, (uint32_t)BOARD_PINS_MIC_CLK_IO,
                 (uint32_t)BOARD_PINS_MIC_DOUT_IO);
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = audio_capture_i2s_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s start fail");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_INIT_FAIL, DIAG_SEV_ERROR, 1, err, 0, 0);
        return err;
    }
    ESP_LOGI(TAG, "i2s start ok");

#ifdef CONFIG_AUDIO_CAPTURE_MIC_ES8311
    err = audio_capture_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c init fail");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_INIT_FAIL, DIAG_SEV_ERROR, 2, err, 0, 0);
        i2s_channel_disable(s_i2s_rx_handle);
        return err;
    }

    err = audio_capture_codec_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec init fail");
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_INIT_FAIL, DIAG_SEV_ERROR, 3, err, 0, 0);
        i2s_channel_disable(s_i2s_rx_handle);
        return err;
    }
    ESP_LOGI(TAG, "codec init ok");
#endif

    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "state mutex create fail");
        i2s_channel_disable(s_i2s_rx_handle);
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    BaseType_t task_ok = xTaskCreate(
        audio_capture_task,
        "audio_capture_task",
        AUDIO_CAPTURE_TASK_STACK_BYTES,
        NULL,
        5,
        &s_capture_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "task create fail");
        i2s_channel_disable(s_i2s_rx_handle);
        vSemaphoreDelete(s_state_mutex);
        s_state_mutex = NULL;
        s_started = false;
        return ESP_ERR_NO_MEM;
    }

#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
    ESP_LOGI(TAG, "audio capture started: SPH0655 PDM digital mic");
#endif

    return ESP_OK;
}

bool audio_capture_is_available(void)
{
    return audio_capture_static_unavailable_reason() == NULL;
}

const char *audio_capture_get_unavailable_reason(void)
{
    return audio_capture_static_unavailable_reason();
}

uint32_t audio_capture_get_frame_count(void)
{
    return s_frame_count;
}

uint32_t audio_capture_get_dropped_frame_count(void)
{
    return s_dropped_frame_count;
}
