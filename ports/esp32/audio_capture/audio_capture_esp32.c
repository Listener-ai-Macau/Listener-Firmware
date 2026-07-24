#include "audio_capture.h"
#include "audio_capture_platform.h"
#include "ble_audio_stream.h"
#include "board_pins.h"
#include "listener_audio_proto.h"
#include "status_led.h"

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
#define AUDIO_CAPTURE_LOG_INTERVAL_FRAMES (500)
#define AUDIO_CAPTURE_TASK_STACK_BYTES  (6 * 1024)
#if CONFIG_FREERTOS_UNICORE
#define AUDIO_CAPTURE_TASK_CORE 0
#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 0
#else
#define AUDIO_CAPTURE_TASK_CORE 1
/* ESP-SR requires feed and fetch to run concurrently. Keep them at equal
 * priority on the audio core: CPU0 carries NimBLE and serial work, and a
 * delayed fetch there lets the AFE overwrite its feed ring. */
#define AUDIO_CAPTURE_AFE_FETCH_TASK_CORE 1
#endif
#define AUDIO_CAPTURE_AFE_FETCH_TASK_PRIORITY 5
#define AUDIO_CAPTURE_V2_MIC_BLOCKER "V2 CLK/GPIO48 DOUT/GPIO47 microphone interface not validated"
/* Recording duration is user-controlled (KEY1 toggle); no fixed upper limit.
 * The only hard limit is uint16_t packet_sequence overflow in the BLE protocol,
 * which is handled gracefully by sending session_stop before overflow. */
#define AUDIO_CAPTURE_STREAM_BATCH_FRAMES 3
#define AUDIO_CAPTURE_STREAM_BATCH_BYTES (AUDIO_CAPTURE_STREAM_BATCH_FRAMES * AUDIO_CAPTURE_FRAME_BYTES)
#define AUDIO_CAPTURE_STREAM_PROGRESS_LOG_PACKET_INTERVAL 64U
#define AUDIO_CAPTURE_IDLE_POWER_SAVE_WAIT_MS 5000U
#define AUDIO_CAPTURE_PDM_HW_AMPLIFY_NUM 8U
/* A modest pre-AFE lift keeps weak physical speech above the WebRTC NS/VAD
 * floor. Fourfold gain clipped real microphone peaks; twofold remains a
 * bounded candidate while Type retains its per-session peak guard. */
#define AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM 2
/* WebRTC AFE accepts 160-sample feed blocks but emits 512-sample output
 * blocks. Four feeds are therefore the minimum to form one fetch result;
 * six frames leave two feed blocks of scheduler headroom without introducing
 * buffered latency in the steady state. */
#define AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES 6
#define AUDIO_CAPTURE_PDM_AFE_FETCH_WAIT_MS 100U
#define AUDIO_CAPTURE_PDM_VAD_FRAME_MS 30U
#define AUDIO_CAPTURE_PDM_VAD_FRAME_SAMPLES \
    ((AUDIO_CAPTURE_SAMPLE_RATE_HZ * AUDIO_CAPTURE_PDM_VAD_FRAME_MS) / 1000U)
#define AUDIO_CAPTURE_VOICE_PREROLL_MAX_MS 600U
#define AUDIO_CAPTURE_VOICE_PREROLL_FRAMES \
    (AUDIO_CAPTURE_VOICE_PREROLL_MAX_MS / AUDIO_CAPTURE_FRAME_MS)
#if defined(CONFIG_AUDIO_CAPTURE_SPH0655_CLK_INVERT) && CONFIG_AUDIO_CAPTURE_SPH0655_CLK_INVERT
#define AUDIO_CAPTURE_SPH0655_CLK_INVERT_ENABLED 1
#else
#define AUDIO_CAPTURE_SPH0655_CLK_INVERT_ENABLED 0
#endif
// Status LED level is a visual envelope. Firmware capture now preserves 4x more
// microphone headroom than the former clipped path, so its display calibration
// must retain the corresponding raw-signal sensitivity without changing PCM.
// Keep the 40-unit silence floor; a 847-unit full scale is a 1.5x steeper
// visual-only response than the preceding 40-to-1250 calibration.
#define AUDIO_CAPTURE_LEVEL_NOISE_FLOOR 40U
#define AUDIO_CAPTURE_LEVEL_FULL_SCALE 847U

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
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_vad.h"
#endif
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
    uint16_t stop_origin;
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
static audio_capture_voice_activity_handler_t s_voice_activity_handler;
static volatile bool s_voice_activation_monitoring;
static int16_t
    s_voice_preroll[AUDIO_CAPTURE_VOICE_PREROLL_FRAMES][AUDIO_CAPTURE_FRAME_SAMPLES];
static uint16_t s_voice_preroll_write_index;
static uint16_t s_voice_preroll_count;
static uint16_t s_session_preroll_start_index;
static uint16_t s_session_preroll_count;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
/* ESP-IDF returns the right PDM slot first in a stereo PCM buffer. Keep the
 * physical SELECT strap separate from that buffer layout so SELECT=GND still
 * means the left microphone slot rather than buffer element zero. */
#if CONFIG_AUDIO_CAPTURE_SPH0655_SLOT_RIGHT
static const int s_pdm_selected_slot = 1;
static const int s_pdm_active_buffer_index = 0;
#else
static const int s_pdm_selected_slot = 0;
static const int s_pdm_active_buffer_index = 1;
#endif
static uint32_t s_pdm_session_raw_slot_peak[2];
static uint64_t s_pdm_session_raw_slot_abs_sum[2];
static uint64_t s_pdm_session_raw_slot_samples;
static uint64_t s_pdm_session_raw_slot_equal_samples;
static uint64_t s_pdm_session_raw_slot_delta_abs_sum;
#endif
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
static const esp_afe_sr_iface_t *s_pdm_afe_handle;
static esp_afe_sr_data_t *s_pdm_afe_data;
static int16_t *s_pdm_afe_feed_buffer;
static int16_t *s_pdm_afe_fetch_buffer;
static size_t s_pdm_afe_feed_samples;
static size_t s_pdm_afe_fetch_samples;
static size_t s_pdm_afe_feed_filled;
static vad_handle_t s_pdm_vad_handle;
static int16_t s_pdm_vad_buffer[AUDIO_CAPTURE_PDM_VAD_FRAME_SAMPLES];
static size_t s_pdm_vad_filled;
static int16_t s_pdm_afe_emit_buffer[AUDIO_CAPTURE_FRAME_SAMPLES];
static size_t s_pdm_afe_emit_filled;
static volatile bool s_pdm_afe_session_boundary_requested;
static volatile bool s_pdm_afe_stop_drain_requested;
static volatile bool s_pdm_afe_stop_drain_feed_cutoff_ack;
static volatile bool s_pdm_afe_stop_drain_complete;
static uint32_t s_pdm_afe_input_frames;
static uint32_t s_pdm_afe_output_frames;
static uint32_t s_pdm_afe_session_input_peak;
static uint64_t s_pdm_afe_session_input_abs_sum;
static uint64_t s_pdm_afe_session_input_samples;
static uint32_t s_pdm_afe_session_output_peak;
static uint64_t s_pdm_afe_session_output_abs_sum;
static uint64_t s_pdm_afe_session_output_samples;
static uint32_t s_pdm_afe_stop_drain_output_frames;
static size_t s_pdm_afe_stop_drain_padding_samples;
static TaskHandle_t s_pdm_afe_fetch_task_handle;
static void audio_capture_log_pdm_afe_session_signal(uint32_t session_id);
#endif
#endif

static bool s_capture_transport_backpressure_active;
static uint32_t s_capture_transport_backpressure_events;

static void audio_capture_wake_task(void)
{
    if (s_capture_task_handle != NULL) {
        xTaskNotifyGive(s_capture_task_handle);
    }
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    if (s_pdm_afe_fetch_task_handle != NULL) {
        xTaskNotifyGive(s_pdm_afe_fetch_task_handle);
    }
#endif
#endif
}

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
    s_capture_transport_backpressure_active = false;
    s_capture_transport_backpressure_events = 0;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    s_pdm_afe_stop_drain_requested = false;
    s_pdm_afe_stop_drain_feed_cutoff_ack = false;
    s_pdm_afe_stop_drain_complete = false;
#endif
#endif
}

static uint8_t audio_capture_frame_level_percent(const int16_t *frame_buffer)
{
    if (frame_buffer == NULL) {
        return 0U;
    }

    uint32_t sum_abs = 0U;
    uint32_t peak_abs = 0U;
    for (size_t index = 0; index < AUDIO_CAPTURE_FRAME_SAMPLES; ++index) {
        int32_t sample = frame_buffer[index];
        uint32_t abs_sample = sample < 0 ? (uint32_t)(-sample) : (uint32_t)sample;
        sum_abs += abs_sample;
        if (abs_sample > peak_abs) {
            peak_abs = abs_sample;
        }
    }

    uint32_t average_abs = sum_abs / AUDIO_CAPTURE_FRAME_SAMPLES;
    uint32_t weighted_level = ((average_abs * 3U) + peak_abs) / 4U;
    if (weighted_level <= AUDIO_CAPTURE_LEVEL_NOISE_FLOOR) {
        return 0U;
    }
    if (weighted_level >= AUDIO_CAPTURE_LEVEL_FULL_SCALE) {
        return 100U;
    }
    return (uint8_t)(((weighted_level - AUDIO_CAPTURE_LEVEL_NOISE_FLOOR) * 100U) /
                     (AUDIO_CAPTURE_LEVEL_FULL_SCALE - AUDIO_CAPTURE_LEVEL_NOISE_FLOOR));
}

static void audio_capture_note_transport_backpressure(void)
{
    ble_audio_stream_backpressure_t pressure = {0};
    ble_audio_stream_get_backpressure(&pressure);

    bool transport_backpressured =
        pressure.transport_session_active && pressure.pause_recommended;
    if (transport_backpressured != s_capture_transport_backpressure_active) {
        s_capture_transport_backpressure_active = transport_backpressured;
        uint32_t session_id = 0;
        if (s_state_mutex != NULL &&
            xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            session_id = s_export_state.session_id;
            xSemaphoreGive(s_state_mutex);
        }
        if (transport_backpressured) {
            s_capture_transport_backpressure_events++;
        }
        ESP_LOGW(
            TAG,
            "record transport backpressure %s: session=%" PRIu32 " queued=%" PRIu32 "/%" PRIu32 " pool=%" PRIu32 "/%" PRIu32 " pressure=%" PRIu32 "%% capture_continues=1 events=%" PRIu32,
            transport_backpressured ? "observed" : "cleared",
            session_id,
            pressure.queue_depth,
            pressure.queue_capacity,
            pressure.audio_pool_in_use,
            pressure.audio_pool_capacity,
            pressure.pressure_percent,
            s_capture_transport_backpressure_events);
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_BACKPRESSURE,
                 transport_backpressured ? DIAG_SEV_WARN : DIAG_SEV_INFO,
                 session_id,
                 transport_backpressured ? 1U : 2U,
                 pressure.queue_depth,
                 pressure.audio_pool_in_use);
    }
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
        /* ESP-IDF returns INVALID_STATE when the channel is already disabled. */
        if (ret == ESP_ERR_INVALID_STATE) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            s_i2s_low_power_disabled = true;
        }
    } else {
        ret = i2s_channel_enable(s_i2s_rx_handle);
        /* ESP-IDF returns INVALID_STATE when the channel is already enabled. */
        if (ret == ESP_ERR_INVALID_STATE) {
            ret = ESP_OK;
        }
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

esp_err_t audio_capture_session_begin_with_preroll(uint32_t pre_roll_ms)
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
    uint32_t requested_pre_roll_frames =
        (pre_roll_ms + AUDIO_CAPTURE_FRAME_MS - 1u) /
        AUDIO_CAPTURE_FRAME_MS;
    if (requested_pre_roll_frames > AUDIO_CAPTURE_VOICE_PREROLL_FRAMES) {
        requested_pre_roll_frames = AUDIO_CAPTURE_VOICE_PREROLL_FRAMES;
    }
    s_session_preroll_count = (uint16_t)requested_pre_roll_frames;
    if (s_session_preroll_count > s_voice_preroll_count) {
        s_session_preroll_count = s_voice_preroll_count;
    }
    s_session_preroll_start_index = (uint16_t)(
        (s_voice_preroll_write_index +
         AUDIO_CAPTURE_VOICE_PREROLL_FRAMES -
         s_session_preroll_count) %
        AUDIO_CAPTURE_VOICE_PREROLL_FRAMES);
    uint32_t session_id = s_export_state.session_id;
    uint32_t session_max_seconds = (s_export_state.total_frames * AUDIO_CAPTURE_FRAME_MS) / 1000U;

#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    /* Apply the session boundary only in the capture task. It clears partial
     * PCM without discarding the already trained NS/AGC state, so first words
     * after an idle wake are processed with the same acoustic calibration as
     * later words. */
    s_pdm_afe_session_boundary_requested = true;
    s_pdm_afe_stop_drain_requested = false;
    s_pdm_afe_stop_drain_feed_cutoff_ack = false;
    s_pdm_afe_stop_drain_complete = false;
    s_pdm_afe_stop_drain_output_frames = 0;
    s_pdm_afe_stop_drain_padding_samples = 0;
    s_pdm_afe_session_input_peak = 0;
    s_pdm_afe_session_input_abs_sum = 0;
    s_pdm_afe_session_input_samples = 0;
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
    memset(s_pdm_session_raw_slot_peak, 0, sizeof(s_pdm_session_raw_slot_peak));
    memset(s_pdm_session_raw_slot_abs_sum, 0, sizeof(s_pdm_session_raw_slot_abs_sum));
    s_pdm_session_raw_slot_samples = 0;
    s_pdm_session_raw_slot_equal_samples = 0;
    s_pdm_session_raw_slot_delta_abs_sum = 0;
#endif
#endif
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
    ESP_LOGI(
            TAG,
            "SPH0655 PDM session slot: session_id=%" PRIu32 " selected=%s",
            session_id,
            s_pdm_selected_slot == 0 ? "left" : "right");
#else
    ESP_LOGI(TAG, "SPH0655 PDM session: session_id=%" PRIu32 " mode=standard-mono", session_id);
#endif
#endif

    s_export_state.requested = true;
    xSemaphoreGive(s_state_mutex);
    audio_capture_wake_task();
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

esp_err_t audio_capture_session_begin(void)
{
    return audio_capture_session_begin_with_preroll(0u);
}

esp_err_t audio_capture_session_stop(void)
{
    return audio_capture_session_stop_with_origin(
        AUDIO_CAPTURE_STOP_ORIGIN_USER);
}

esp_err_t audio_capture_session_stop_with_origin(
    audio_capture_stop_origin_t origin)
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
    s_export_state.stop_origin = (uint16_t)origin;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    s_pdm_afe_stop_drain_requested = true;
    s_pdm_afe_stop_drain_feed_cutoff_ack = false;
    s_pdm_afe_stop_drain_complete = false;
#endif
#endif
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(TAG, "record session stop requested origin=%u", (unsigned)origin);
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
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    s_pdm_afe_stop_drain_requested = false;
    s_pdm_afe_stop_drain_feed_cutoff_ack = false;
    s_pdm_afe_stop_drain_complete = false;
#endif
#endif
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
    if (enabled) {
        s_voice_activation_monitoring = false;
        if (s_state_mutex != NULL &&
            xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            s_voice_preroll_write_index = 0u;
            s_voice_preroll_count = 0u;
            s_session_preroll_count = 0u;
            memset(s_voice_preroll, 0, sizeof(s_voice_preroll));
            xSemaphoreGive(s_state_mutex);
        }
    }
    s_idle_power_save_requested = enabled;
    audio_capture_wake_task();
    if (!enabled) {
        return audio_capture_apply_idle_power_save(false);
    }
    if (audio_capture_session_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void audio_capture_set_voice_activity_handler(
    audio_capture_voice_activity_handler_t handler)
{
    s_voice_activity_handler = handler;
}

esp_err_t audio_capture_set_voice_activation_monitoring(bool enabled)
{
    if (enabled && s_idle_power_save_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    s_voice_activation_monitoring = enabled;
    if (!enabled &&
        s_state_mutex != NULL &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_voice_preroll_write_index = 0u;
        s_voice_preroll_count = 0u;
        s_session_preroll_count = 0u;
        memset(s_voice_preroll, 0, sizeof(s_voice_preroll));
        xSemaphoreGive(s_state_mutex);
    }
    return ESP_OK;
}

bool audio_capture_voice_activation_monitoring_is_enabled(void)
{
    return s_voice_activation_monitoring;
}

/* ---------- Shared frame processing ---------- */

static esp_err_t audio_capture_send_session_preroll(
    uint32_t session_id,
    uint16_t frame_count)
{
    uint16_t packet_sequence = 0u;
    uint16_t emitted_frames = 0u;
    uint8_t batch[AUDIO_CAPTURE_STREAM_BATCH_BYTES];
    while (emitted_frames < frame_count) {
        uint16_t batch_frames =
            (uint16_t)(frame_count - emitted_frames);
        if (batch_frames > AUDIO_CAPTURE_STREAM_BATCH_FRAMES) {
            batch_frames = AUDIO_CAPTURE_STREAM_BATCH_FRAMES;
        }
        for (uint16_t i = 0u; i < batch_frames; ++i) {
            uint16_t ring_index = (uint16_t)(
                (s_session_preroll_start_index + emitted_frames + i) %
                AUDIO_CAPTURE_VOICE_PREROLL_FRAMES);
            memcpy(
                batch + ((size_t)i * AUDIO_CAPTURE_FRAME_BYTES),
                s_voice_preroll[ring_index],
                AUDIO_CAPTURE_FRAME_BYTES);
        }
        uint16_t pcm_bytes =
            (uint16_t)(batch_frames * AUDIO_CAPTURE_FRAME_BYTES);
        uint16_t packet_count =
            ble_audio_stream_count_audio_packets(batch, pcm_bytes);
        esp_err_t ret = ble_audio_stream_send_session_audio(
            session_id,
            packet_sequence,
            batch,
            pcm_bytes,
            packet_count);
        if (ret != ESP_OK) {
            return ret;
        }
        packet_sequence = (uint16_t)(packet_sequence + packet_count);
        emitted_frames = (uint16_t)(emitted_frames + batch_frames);
    }
    return ESP_OK;
}

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
    uint16_t session_stop_origin = LISTENER_AUDIO_SESSION_STOP_ORIGIN_USER;
    uint16_t session_error_code = LISTENER_AUDIO_SESSION_ERROR_NONE;
    uint16_t packet_count = 0;
    const uint8_t *audio_batch_copy = NULL;

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_voice_activation_monitoring &&
            !s_export_state.requested &&
            !s_export_state.active) {
            memcpy(
                s_voice_preroll[s_voice_preroll_write_index],
                frame_buffer,
                AUDIO_CAPTURE_FRAME_BYTES);
            s_voice_preroll_write_index = (uint16_t)(
                (s_voice_preroll_write_index + 1u) %
                AUDIO_CAPTURE_VOICE_PREROLL_FRAMES);
            if (s_voice_preroll_count < AUDIO_CAPTURE_VOICE_PREROLL_FRAMES) {
                s_voice_preroll_count++;
            }
        }
        if (s_export_state.requested && !s_export_state.active) {
            s_export_state.requested = false;
            s_export_state.active = true;
            s_export_state.captured_frames = 0;
            s_export_state.pcm_bytes_written = 0;
            s_export_state.stream_next_packet_sequence = 0;
            s_export_state.stream_batch_frame_count = 0;
            s_export_state.ble_session_started = false;
            if (s_session_preroll_count > 0u) {
                uint16_t pre_roll_packet_count = 0u;
                uint16_t counted_frames = 0u;
                while (counted_frames < s_session_preroll_count) {
                    uint16_t batch_frames =
                        (uint16_t)(s_session_preroll_count - counted_frames);
                    if (batch_frames > AUDIO_CAPTURE_STREAM_BATCH_FRAMES) {
                        batch_frames = AUDIO_CAPTURE_STREAM_BATCH_FRAMES;
                    }
                    uint8_t batch[AUDIO_CAPTURE_STREAM_BATCH_BYTES];
                    for (uint16_t i = 0u; i < batch_frames; ++i) {
                        uint16_t ring_index = (uint16_t)(
                            (s_session_preroll_start_index +
                             counted_frames + i) %
                            AUDIO_CAPTURE_VOICE_PREROLL_FRAMES);
                        memcpy(
                            batch + ((size_t)i * AUDIO_CAPTURE_FRAME_BYTES),
                            s_voice_preroll[ring_index],
                            AUDIO_CAPTURE_FRAME_BYTES);
                    }
                    uint16_t pcm_bytes =
                        (uint16_t)(batch_frames * AUDIO_CAPTURE_FRAME_BYTES);
                    pre_roll_packet_count = (uint16_t)(
                        pre_roll_packet_count +
                        ble_audio_stream_count_audio_packets(
                            batch,
                            pcm_bytes));
                    counted_frames =
                        (uint16_t)(counted_frames + batch_frames);
                }
                s_export_state.stream_next_packet_sequence =
                    pre_roll_packet_count;
                s_export_state.captured_frames =
                    s_session_preroll_count;
                s_export_state.pcm_bytes_written =
                    (size_t)s_session_preroll_count *
                    AUDIO_CAPTURE_FRAME_BYTES;
            }
        }

        if (s_export_state.active && s_export_state.captured_frames < s_export_state.total_frames) {
            bool stop_boundary_requested =
                s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION &&
                s_export_state.stop_requested;
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
            if (stop_boundary_requested && s_pdm_afe_stop_drain_requested &&
                !s_pdm_afe_stop_drain_complete) {
                stop_boundary_requested = false;
            }
#endif
#endif
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
                        packet_count = ble_audio_stream_count_audio_packets(audio_batch_copy, batch_pcm_bytes);
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
                    packet_count = ble_audio_stream_count_audio_packets(audio_batch_copy, batch_pcm_bytes);
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
                    session_stop_origin = s_export_state.stop_origin;
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

    status_led_set_recording_level(audio_capture_frame_level_percent(frame_buffer));

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
                if (s_session_preroll_count > 0u) {
                    esp_err_t pre_roll_ret =
                        audio_capture_send_session_preroll(
                            session_id,
                            s_session_preroll_count);
                    if (pre_roll_ret != ESP_OK) {
                        stream_failed = true;
                        should_transport_error = true;
                        session_error_code =
                            audio_capture_session_error_from_stream_result(
                                pre_roll_ret);
                        ESP_LOGW(
                            TAG,
                            "stream session preroll failed: session_id=%" PRIu32
                            " frames=%u ret=%s",
                            session_id,
                            s_session_preroll_count,
                            esp_err_to_name(pre_roll_ret));
                    } else {
                        ESP_LOGI(
                            TAG,
                            "stream session preroll queued: session_id=%" PRIu32
                            " frames=%u ms=%u",
                            session_id,
                            s_session_preroll_count,
                            s_session_preroll_count *
                                AUDIO_CAPTURE_FRAME_MS);
                    }
                    memset(s_voice_preroll, 0, sizeof(s_voice_preroll));
                    s_voice_preroll_write_index = 0u;
                    s_voice_preroll_count = 0u;
                    s_session_preroll_count = 0u;
                }
            }
        }

        if (!stream_failed && should_session_audio) {
            esp_err_t audio_ret =
                ble_audio_stream_send_session_audio(
                    session_id,
                    packet_sequence_start,
                    audio_batch_copy,
                    batch_pcm_bytes,
                    packet_count);
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
            esp_err_t stop_ret = ble_audio_stream_send_session_stop_with_origin(
                session_id,
                expected_packet_count_at_end,
                session_stop_origin);
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
            uint32_t stop_transport_backpressure_events = 0;
            if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
                stop_duration = s_export_state.duration_seconds;
                stop_frames = s_export_state.captured_frames;
                stop_transport_backpressure_events = s_capture_transport_backpressure_events;
                audio_capture_export_cleanup();
                xSemaphoreGive(s_state_mutex);
            }
            ESP_LOGI(
                TAG,
                "record session capture integrity: session_id=%" PRIu32 " pcm_ms=%" PRIu32 " capture_backpressure_gap_ms=0 capture_backpressure_pause_frames=0 transport_backpressure_events=%" PRIu32,
                session_id,
                stop_frames * AUDIO_CAPTURE_FRAME_MS,
                stop_transport_backpressure_events);
#ifdef CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
            audio_capture_log_pdm_afe_session_signal(session_id);
#endif
#endif
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
            (void)watchdog_platform_task_notify_take_low_power(
                pdTRUE,
                AUDIO_CAPTURE_IDLE_POWER_SAVE_WAIT_MS);
            continue;
        }
        (void)audio_capture_apply_idle_power_save(false);

        audio_capture_note_transport_backpressure();
        int ret = esp_codec_dev_read(s_codec_handle, frame_buffer, sizeof(frame_buffer));
        if (ret == ESP_CODEC_DEV_OK) {
            audio_capture_note_transport_backpressure();
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

/* A SPH0655 drives one slot according to its soldered SELECT strap. Keep the
 * board configuration deterministic: boot-time ambient noise cannot identify
 * a hardware strap reliably and must never change a device's recording route. */

static int16_t audio_capture_scale_sample(int16_t sample)
{
    int32_t scaled = (int32_t)sample * AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM;
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static void audio_capture_apply_pdm_software_gain(int16_t *frame_buffer)
{
    if (AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM <= 1) {
        return;
    }
    for (size_t i = 0; i < AUDIO_CAPTURE_FRAME_SAMPLES; i++) {
        frame_buffer[i] = audio_capture_scale_sample(frame_buffer[i]);
    }
}

#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
static void audio_capture_select_pdm_slot(const int16_t *interleaved, int16_t *mono)
{
    for (size_t i = 0; i < AUDIO_CAPTURE_FRAME_SAMPLES; i++) {
        mono[i] = interleaved[(i * 2U) + (size_t)s_pdm_active_buffer_index];
    }
}

#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
static void audio_capture_note_pdm_session_raw_slots(
    const int16_t *interleaved,
    size_t samples_per_slot)
{
    if ((!s_export_state.requested && !s_export_state.active) || interleaved == NULL) {
        return;
    }

    for (size_t index = 0; index < samples_per_slot; index++) {
        int32_t left = interleaved[index * 2U];
        int32_t right = interleaved[(index * 2U) + 1U];
        int32_t delta = left - right;
        if (delta == 0) {
            s_pdm_session_raw_slot_equal_samples++;
        }
        s_pdm_session_raw_slot_delta_abs_sum += (uint32_t)(delta < 0 ? -delta : delta);
        for (size_t slot = 0; slot < 2U; slot++) {
            int32_t value = interleaved[(index * 2U) + slot];
            uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
            if (magnitude > s_pdm_session_raw_slot_peak[slot]) {
                s_pdm_session_raw_slot_peak[slot] = magnitude;
            }
            s_pdm_session_raw_slot_abs_sum[slot] += magnitude;
        }
    }
    s_pdm_session_raw_slot_samples += samples_per_slot;
}
#endif
#endif

#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
static void audio_capture_log_pdm_afe_level(
    const char *stage,
    uint32_t block_index,
    const int16_t *samples,
    size_t sample_count)
{
    if (block_index == 0U || block_index > 100U || (block_index % 5U) != 0U) {
        return;
    }

    uint32_t peak = 0U;
    uint64_t magnitude_sum = 0U;
    for (size_t index = 0; index < sample_count; index++) {
        int32_t value = samples[index];
        uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
        if (magnitude > peak) {
            peak = magnitude;
        }
        magnitude_sum += magnitude;
    }

    ESP_LOGI(
        TAG,
        "PDM AFE level: stage=%s block=%" PRIu32 " peak=%" PRIu32 " mean_abs=%" PRIu32,
        stage,
        block_index,
        peak,
        sample_count == 0U ? 0U : (uint32_t)(magnitude_sum / sample_count));
}

static void audio_capture_note_pdm_afe_session_output(
    const int16_t *samples,
    size_t sample_count)
{
    uint32_t peak = 0U;
    uint64_t magnitude_sum = 0U;
    for (size_t index = 0; index < sample_count; index++) {
        int32_t value = samples[index];
        uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
        if (magnitude > peak) {
            peak = magnitude;
        }
        magnitude_sum += magnitude;
    }
    if (peak > s_pdm_afe_session_output_peak) {
        s_pdm_afe_session_output_peak = peak;
    }
    s_pdm_afe_session_output_abs_sum += magnitude_sum;
    s_pdm_afe_session_output_samples += sample_count;
}

static void audio_capture_note_pdm_afe_session_input(
    const int16_t *samples,
    size_t sample_count)
{
    if ((!s_export_state.requested && !s_export_state.active) || samples == NULL) {
        return;
    }

    uint32_t peak = 0U;
    uint64_t magnitude_sum = 0U;
    for (size_t index = 0; index < sample_count; index++) {
        int32_t value = samples[index];
        uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
        if (magnitude > peak) {
            peak = magnitude;
        }
        magnitude_sum += magnitude;
    }
    if (peak > s_pdm_afe_session_input_peak) {
        s_pdm_afe_session_input_peak = peak;
    }
    s_pdm_afe_session_input_abs_sum += magnitude_sum;
    s_pdm_afe_session_input_samples += sample_count;
}

static void audio_capture_log_pdm_afe_session_signal(uint32_t session_id)
{
    uint32_t input_mean_abs = s_pdm_afe_session_input_samples == 0U
        ? 0U
        : (uint32_t)(s_pdm_afe_session_input_abs_sum / s_pdm_afe_session_input_samples);
    uint32_t mean_abs = s_pdm_afe_session_output_samples == 0U
        ? 0U
        : (uint32_t)(s_pdm_afe_session_output_abs_sum / s_pdm_afe_session_output_samples);
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
    uint32_t selected_raw_mean_abs = s_pdm_session_raw_slot_samples == 0U
        ? 0U
        : (uint32_t)(s_pdm_session_raw_slot_abs_sum[s_pdm_active_buffer_index] /
                     s_pdm_session_raw_slot_samples);
    uint32_t alternate_raw_mean_abs = s_pdm_session_raw_slot_samples == 0U
        ? 0U
        : (uint32_t)(s_pdm_session_raw_slot_abs_sum[1 - s_pdm_active_buffer_index] /
                     s_pdm_session_raw_slot_samples);
    uint32_t raw_slot_equal_permille = s_pdm_session_raw_slot_samples == 0U
        ? 0U
        : (uint32_t)((s_pdm_session_raw_slot_equal_samples * 1000U) /
                     s_pdm_session_raw_slot_samples);
    uint32_t raw_slot_delta_mean_abs = s_pdm_session_raw_slot_samples == 0U
        ? 0U
        : (uint32_t)(s_pdm_session_raw_slot_delta_abs_sum /
                     s_pdm_session_raw_slot_samples);
#endif
    ESP_LOGI(
        TAG,
        "PDM AFE session signal: session_id=%" PRIu32 " input_peak=%" PRIu32
        " input_mean_abs=%" PRIu32 " output_frames=%" PRIu32 " output_peak=%" PRIu32
        " output_mean_abs=%" PRIu32
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
        " raw_selected_peak=%" PRIu32 " raw_selected_mean_abs=%" PRIu32
        " raw_alternate_peak=%" PRIu32 " raw_alternate_mean_abs=%" PRIu32
        " raw_slot_equal_permille=%" PRIu32 " raw_slot_delta_mean_abs=%" PRIu32
#endif
        " stop_drain_frames=%" PRIu32
        " stop_drain_padding_samples=%u",
        session_id,
        s_pdm_afe_session_input_peak,
        input_mean_abs,
        s_pdm_afe_output_frames,
        s_pdm_afe_session_output_peak,
        mean_abs,
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
        s_pdm_session_raw_slot_peak[s_pdm_active_buffer_index],
        selected_raw_mean_abs,
        s_pdm_session_raw_slot_peak[1 - s_pdm_active_buffer_index],
        alternate_raw_mean_abs,
        raw_slot_equal_permille,
        raw_slot_delta_mean_abs,
#endif
        s_pdm_afe_stop_drain_output_frames,
        (unsigned)s_pdm_afe_stop_drain_padding_samples);
}

static void audio_capture_pdm_afe_emit(const int16_t *samples, size_t sample_count)
{
    while (sample_count > 0) {
        size_t remaining = AUDIO_CAPTURE_FRAME_SAMPLES - s_pdm_afe_emit_filled;
        size_t copied = sample_count < remaining ? sample_count : remaining;
        memcpy(
            s_pdm_afe_emit_buffer + s_pdm_afe_emit_filled,
            samples,
            copied * sizeof(*samples));
        s_pdm_afe_emit_filled += copied;
        samples += copied;
        sample_count -= copied;

        if (s_pdm_afe_emit_filled == AUDIO_CAPTURE_FRAME_SAMPLES) {
            if (s_pdm_afe_output_frames == 0U) {
                ESP_LOGI(
                    TAG,
                    "PDM AFE first enhanced frame: feed_blocks=%" PRIu32,
                    s_pdm_afe_input_frames);
            }
            audio_capture_log_pdm_afe_level(
                "output",
                s_pdm_afe_output_frames + 1U,
                s_pdm_afe_emit_buffer,
                AUDIO_CAPTURE_FRAME_SAMPLES);
            audio_capture_note_pdm_afe_session_output(
                s_pdm_afe_emit_buffer,
                AUDIO_CAPTURE_FRAME_SAMPLES);
            audio_capture_process_frame(s_pdm_afe_emit_buffer);
            if (s_pdm_afe_stop_drain_requested && !s_pdm_afe_stop_drain_complete) {
                s_pdm_afe_stop_drain_output_frames++;
            }
            s_pdm_afe_emit_filled = 0;
            s_pdm_afe_output_frames++;
        }
    }
}

static void audio_capture_pdm_afe_complete_stop_drain(void)
{
    if (!s_pdm_afe_stop_drain_requested || !s_pdm_afe_stop_drain_feed_cutoff_ack ||
        s_pdm_afe_stop_drain_complete) {
        return;
    }

    if (s_pdm_afe_emit_filled > 0U) {
        int16_t silence[AUDIO_CAPTURE_FRAME_SAMPLES] = {0};
        size_t padding_samples = AUDIO_CAPTURE_FRAME_SAMPLES - s_pdm_afe_emit_filled;
        s_pdm_afe_stop_drain_padding_samples = padding_samples;
        audio_capture_pdm_afe_emit(silence, padding_samples);
    }

    s_pdm_afe_stop_drain_complete = true;
    int16_t boundary_frame[AUDIO_CAPTURE_FRAME_SAMPLES] = {0};
    audio_capture_process_frame(boundary_frame);
}

static void audio_capture_pdm_vad_process(
    const int16_t *samples,
    size_t sample_count)
{
    if ((!s_voice_activation_monitoring &&
         !audio_capture_session_is_active()) ||
        s_pdm_vad_handle == NULL) {
        s_pdm_vad_filled = 0u;
        return;
    }

    while (sample_count > 0u) {
        size_t remaining =
            AUDIO_CAPTURE_PDM_VAD_FRAME_SAMPLES - s_pdm_vad_filled;
        size_t copied = sample_count < remaining ? sample_count : remaining;
        memcpy(
            s_pdm_vad_buffer + s_pdm_vad_filled,
            samples,
            copied * sizeof(*samples));
        s_pdm_vad_filled += copied;
        samples += copied;
        sample_count -= copied;

        if (s_pdm_vad_filled == AUDIO_CAPTURE_PDM_VAD_FRAME_SAMPLES) {
            vad_state_t state = vad_process(
                s_pdm_vad_handle,
                s_pdm_vad_buffer,
                AUDIO_CAPTURE_SAMPLE_RATE_HZ,
                AUDIO_CAPTURE_PDM_VAD_FRAME_MS);
            audio_capture_voice_activity_handler_t handler =
                s_voice_activity_handler;
            if (handler != NULL) {
                handler(
                    state == VAD_SPEECH,
                    AUDIO_CAPTURE_PDM_VAD_FRAME_MS);
            }
            s_pdm_vad_filled = 0u;
        }
    }
}

/* ESP-SR requires its feed and fetch calls to run concurrently. Keep this
 * task separate from I2S capture; serializing them stalls feed and leaves the
 * user with a recording state that never emits PCM. */
static void audio_capture_pdm_afe_fetch_task(void *arg)
{
    (void)arg;
    (void)watchdog_platform_subscribe_current_task("audio_afe_fetch_task");

    while (true) {
        watchdog_platform_feed_current_task();
        /* I2S is deliberately stopped in idle power save. Do not poll the AFE
         * without an input producer: its empty-ring-buffer warning is both
         * misleading and unnecessary work. audio_capture_wake_task() wakes
         * this fetch task together with capture when a session starts. */
        if (s_idle_power_save_requested && !audio_capture_session_is_active()) {
            (void)watchdog_platform_task_notify_take_low_power(
                pdTRUE,
                AUDIO_CAPTURE_IDLE_POWER_SAVE_WAIT_MS);
            continue;
        }
        size_t sample_count = 0;
        afe_fetch_result_t *result =
            s_pdm_afe_handle->fetch_with_delay(
                s_pdm_afe_data,
                pdMS_TO_TICKS(AUDIO_CAPTURE_PDM_AFE_FETCH_WAIT_MS));
        if (result == NULL || result->ret_value != ESP_OK) {
            audio_capture_pdm_afe_complete_stop_drain();
            continue;
        }
        if (!s_pdm_afe_session_boundary_requested && result->data != NULL && result->data_size > 0) {
            sample_count = (size_t)result->data_size / sizeof(*result->data);
            if (sample_count <= s_pdm_afe_fetch_samples) {
                memcpy(
                    s_pdm_afe_fetch_buffer,
                    result->data,
                    sample_count * sizeof(*result->data));
            } else {
                ESP_LOGW(
                    TAG,
                    "PDM AFE fetch frame too large: samples=%u capacity=%u",
                    (unsigned)sample_count,
                    (unsigned)s_pdm_afe_fetch_samples);
                sample_count = 0;
            }
        }
        if (sample_count > 0) {
            audio_capture_pdm_vad_process(
                s_pdm_afe_fetch_buffer,
                sample_count);
            audio_capture_pdm_afe_emit(s_pdm_afe_fetch_buffer, sample_count);
        }
    }
}

static esp_err_t audio_capture_pdm_afe_init(void)
{
    afe_config_t *config = afe_config_init("M", NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    if (config == NULL) {
        ESP_LOGE(TAG, "PDM AFE config allocation failed");
        return ESP_ERR_NO_MEM;
    }

    /* The PDM board has one microphone and no playback reference. Keep VAD
     * outside AFE so classification cannot stall its feed/fetch pipeline. */
    config->aec_init = false;
    config->se_init = false;
    config->ns_init = true;
    config->ns_model_name = NULL;
    config->afe_ns_mode = AFE_NS_MODE_WEBRTC;
    config->vad_init = false;
    config->wakenet_init = false;
    /* Keep hardware WebRTC noise suppression, but leave streaming gain control
     * to Type so the microphone path is not compressed twice. */
    config->agc_init = false;
    config->agc_mode = AFE_AGC_MODE_WEBRTC;
    config->agc_compression_gain_db = 9;
    config->agc_target_level_dbfs = 3;
    config->afe_ringbuf_size = AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    /* Preserve the post-NS waveform. A fixed 12 dB lift can clip ordinary
     * microphone peaks before Type has a chance to calibrate the session. */
    config->afe_linear_gain = 1.0f;
    config->fixed_first_channel = true;
    config->fixed_output_channel = true;
    config = afe_config_check(config);
    if (config == NULL) {
        ESP_LOGE(TAG, "PDM AFE config rejected");
        return ESP_FAIL;
    }

    const float linear_gain = config->afe_linear_gain;
    s_pdm_afe_handle = esp_afe_handle_from_config(config);
    if (s_pdm_afe_handle == NULL) {
        ESP_LOGE(TAG, "PDM AFE handle unavailable");
        afe_config_free(config);
        return ESP_FAIL;
    }

    s_pdm_afe_data = s_pdm_afe_handle->create_from_config(config);
    afe_config_free(config);
    if (s_pdm_afe_data == NULL) {
        ESP_LOGE(TAG, "PDM AFE instance creation failed");
        s_pdm_afe_handle = NULL;
        return ESP_FAIL;
    }

    int feed_samples = s_pdm_afe_handle->get_feed_chunksize(s_pdm_afe_data);
    int fetch_samples = s_pdm_afe_handle->get_fetch_chunksize(s_pdm_afe_data);
    if (feed_samples <= 0 || fetch_samples <= 0) {
        ESP_LOGE(TAG, "PDM AFE invalid frame geometry: feed=%d fetch=%d", feed_samples, fetch_samples);
        s_pdm_afe_handle->destroy(s_pdm_afe_data);
        s_pdm_afe_data = NULL;
        s_pdm_afe_handle = NULL;
        return ESP_FAIL;
    }

    s_pdm_afe_feed_buffer = calloc((size_t)feed_samples, sizeof(*s_pdm_afe_feed_buffer));
    s_pdm_afe_fetch_buffer = calloc((size_t)fetch_samples, sizeof(*s_pdm_afe_fetch_buffer));
    if (s_pdm_afe_feed_buffer == NULL || s_pdm_afe_fetch_buffer == NULL) {
        ESP_LOGE(TAG, "PDM AFE buffer allocation failed: feed=%d fetch=%d", feed_samples, fetch_samples);
        free(s_pdm_afe_feed_buffer);
        free(s_pdm_afe_fetch_buffer);
        s_pdm_afe_feed_buffer = NULL;
        s_pdm_afe_fetch_buffer = NULL;
        s_pdm_afe_handle->destroy(s_pdm_afe_data);
        s_pdm_afe_data = NULL;
        s_pdm_afe_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_pdm_afe_feed_samples = (size_t)feed_samples;
    s_pdm_afe_fetch_samples = (size_t)fetch_samples;
    s_pdm_vad_handle = vad_create(VAD_MODE_3);
    if (s_pdm_vad_handle == NULL) {
        ESP_LOGE(TAG, "PDM WebRTC VAD allocation failed");
        free(s_pdm_afe_feed_buffer);
        free(s_pdm_afe_fetch_buffer);
        s_pdm_afe_feed_buffer = NULL;
        s_pdm_afe_fetch_buffer = NULL;
        s_pdm_afe_handle->destroy(s_pdm_afe_data);
        s_pdm_afe_data = NULL;
        s_pdm_afe_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    BaseType_t task_ok = xTaskCreatePinnedToCore(
        audio_capture_pdm_afe_fetch_task,
        "audio_afe_fetch",
        AUDIO_CAPTURE_TASK_STACK_BYTES,
        NULL,
        /* Match I2S capture priority on the other core so every feed block is
         * consumed before the AFE ring can overwrite audio. */
        AUDIO_CAPTURE_AFE_FETCH_TASK_PRIORITY,
        &s_pdm_afe_fetch_task_handle,
        AUDIO_CAPTURE_AFE_FETCH_TASK_CORE);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "PDM AFE fetch task creation failed");
        vad_destroy(s_pdm_vad_handle);
        s_pdm_vad_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_pdm_afe_handle->print_pipeline(s_pdm_afe_data);
    ESP_LOGI(
        TAG,
        "PDM AFE ready: pipeline=continuous-webrtc-ns standalone_vad=webrtc-mode3 vad_frame_ms=%u linear_gain=%.1f feed_samples=%d fetch_samples=%d ringbuf_frames=%u fetch_wait_ms=%u feed_core=%d feed_prio=5 fetch_core=%d fetch_prio=%u",
        (unsigned)AUDIO_CAPTURE_PDM_VAD_FRAME_MS,
        (double)linear_gain,
        feed_samples,
        fetch_samples,
        (unsigned)AUDIO_CAPTURE_PDM_AFE_RINGBUF_FRAMES,
        (unsigned)AUDIO_CAPTURE_PDM_AFE_FETCH_WAIT_MS,
        AUDIO_CAPTURE_TASK_CORE,
        AUDIO_CAPTURE_AFE_FETCH_TASK_CORE,
        (unsigned)AUDIO_CAPTURE_AFE_FETCH_TASK_PRIORITY);
    return ESP_OK;
}

static void audio_capture_pdm_afe_apply_session_boundary_if_requested(void)
{
    if (!s_pdm_afe_session_boundary_requested) {
        return;
    }

    s_pdm_afe_feed_filled = 0;
    s_pdm_afe_emit_filled = 0;
    s_pdm_afe_input_frames = 0;
    s_pdm_afe_output_frames = 0;
    s_pdm_afe_session_output_peak = 0;
    s_pdm_afe_session_output_abs_sum = 0;
    s_pdm_afe_session_output_samples = 0;
    s_pdm_afe_session_boundary_requested = false;
    ESP_LOGI(TAG, "PDM AFE session boundary: preserving trained NS/AGC state");
}

static void audio_capture_pdm_afe_process(const int16_t *frame_buffer)
{
    audio_capture_pdm_afe_apply_session_boundary_if_requested();

    if (s_pdm_afe_stop_drain_requested) {
        /* The capture task has observed the stop edge. It must not feed post-stop
         * input, while the fetch task drains enhanced output already in the AFE. */
        s_pdm_afe_stop_drain_feed_cutoff_ack = true;
        return;
    }

    audio_capture_note_pdm_afe_session_input(frame_buffer, AUDIO_CAPTURE_FRAME_SAMPLES);

    size_t remaining = AUDIO_CAPTURE_FRAME_SAMPLES;
    const int16_t *cursor = frame_buffer;
    while (remaining > 0) {
        size_t capacity = s_pdm_afe_feed_samples - s_pdm_afe_feed_filled;
        size_t copied = remaining < capacity ? remaining : capacity;
        memcpy(
            s_pdm_afe_feed_buffer + s_pdm_afe_feed_filled,
            cursor,
            copied * sizeof(*cursor));
        s_pdm_afe_feed_filled += copied;
        cursor += copied;
        remaining -= copied;

        if (s_pdm_afe_feed_filled == s_pdm_afe_feed_samples) {
            int ret = s_pdm_afe_handle->feed(s_pdm_afe_data, s_pdm_afe_feed_buffer);
            if (ret < 0) {
                ESP_LOGW(TAG, "PDM AFE feed failed: ret=%d", ret);
            }
            s_pdm_afe_feed_filled = 0;
            s_pdm_afe_input_frames++;
            audio_capture_log_pdm_afe_level(
                "input",
                s_pdm_afe_input_frames,
                s_pdm_afe_feed_buffer,
                s_pdm_afe_feed_samples);
        }
    }
}
#endif

static void audio_capture_task(void *arg)
{
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
    int16_t interleaved_frame_buffer[AUDIO_CAPTURE_FRAME_SAMPLES * 2U];
#endif
    int16_t frame_buffer[AUDIO_CAPTURE_FRAME_SAMPLES];
    (void)watchdog_platform_subscribe_current_task("audio_capture_task");

    while (1) {
        watchdog_platform_feed_current_task();
        if (s_idle_power_save_requested && !audio_capture_session_is_active()) {
            (void)audio_capture_apply_idle_power_save(true);
            (void)watchdog_platform_task_notify_take_low_power(
                pdTRUE,
                AUDIO_CAPTURE_IDLE_POWER_SAVE_WAIT_MS);
            continue;
        }
        (void)audio_capture_apply_idle_power_save(false);

        audio_capture_note_transport_backpressure();
        size_t bytes_read = 0;
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
        esp_err_t ret = i2s_channel_read(
            s_i2s_rx_handle, interleaved_frame_buffer, sizeof(interleaved_frame_buffer),
            &bytes_read, portMAX_DELAY);
#else
        esp_err_t ret = i2s_channel_read(
            s_i2s_rx_handle, frame_buffer, sizeof(frame_buffer),
            &bytes_read, portMAX_DELAY);
#endif
        if (ret == ESP_OK && bytes_read == sizeof(
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
                interleaved_frame_buffer
#else
                frame_buffer
#endif
            )) {
            audio_capture_note_transport_backpressure();
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
 #if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
            audio_capture_note_pdm_session_raw_slots(
                interleaved_frame_buffer,
                AUDIO_CAPTURE_FRAME_SAMPLES);
 #endif
            audio_capture_select_pdm_slot(interleaved_frame_buffer, frame_buffer);
#endif
            audio_capture_apply_pdm_software_gain(frame_buffer);
#if CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
            audio_capture_pdm_afe_process(frame_buffer);
#else
            audio_capture_process_frame(frame_buffer);
#endif
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
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
#else
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#endif
        .gpio_cfg = {
            .clk = BOARD_PINS_MIC_CLK_IO,
            .din = BOARD_PINS_MIC_DOUT_IO,
            .invert_flags = {
                .clk_inv = AUDIO_CAPTURE_SPH0655_CLK_INVERT_ENABLED,
            },
        },
    };
    /* SPH0655 requires a PDM clock of at least 1.1 MHz. The IDF default 8S
     * setting would generate 1.024 MHz at 16 kHz PCM, below that range.
     * 16S keeps the 16 kHz output contract while driving the mic at 2.048 MHz. */
    pdm_cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
#if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
    pdm_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_BOTH;
#endif
#if defined(SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER) && SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_cfg.slot_cfg.hp_en = true;
    pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 35.5f;
    pdm_cfg.slot_cfg.amplify_num = AUDIO_CAPTURE_PDM_HW_AMPLIFY_NUM;
#endif

    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_i2s_rx_handle, &pdm_cfg), TAG, "init pdm rx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx_handle), TAG, "enable pdm rx failed");
    ESP_LOGI(
        TAG,
        "SPH0655 V2.2 PDM contract: select=GND data_edge=falling active_slot=%s clk_invert=%u pcm=%uHz pdm_clk=%uHz dsr=16S clk=%d din=%d hp_filter=%u hw_amplify=%u sw_gain=%u",
 #if CONFIG_AUDIO_CAPTURE_PDM_STEREO_SLOT_CAPTURE
        s_pdm_selected_slot == 0 ? "left" : "right",
 #else
        "standard-mono",
 #endif
        (unsigned)AUDIO_CAPTURE_SPH0655_CLK_INVERT_ENABLED,
        AUDIO_CAPTURE_SAMPLE_RATE_HZ,
        AUDIO_CAPTURE_SAMPLE_RATE_HZ * 128U,
        (int)BOARD_PINS_MIC_CLK_IO,
        (int)BOARD_PINS_MIC_DOUT_IO,
#if defined(SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER) && SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
        1u,
        AUDIO_CAPTURE_PDM_HW_AMPLIFY_NUM,
        (unsigned)AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM
#else
        0u,
        1u,
        (unsigned)AUDIO_CAPTURE_PDM_SOFTWARE_GAIN_NUM
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

#if defined(CONFIG_AUDIO_CAPTURE_MIC_SPH0655_PDM) && CONFIG_AUDIO_CAPTURE_PDM_AFE_WEBRTC
    err = audio_capture_pdm_afe_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PDM AFE init fail: %s", esp_err_to_name(err));
        diag_log(DIAG_SRC_AUDIO, DIAG_AUDIO_INIT_FAIL, DIAG_SEV_ERROR, 9, err, 0, 0);
        i2s_channel_disable(s_i2s_rx_handle);
        vSemaphoreDelete(s_state_mutex);
        s_state_mutex = NULL;
        s_started = false;
        return err;
    }
#endif

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        audio_capture_task,
        "audio_capture_task",
        AUDIO_CAPTURE_TASK_STACK_BYTES,
        NULL,
        5,
        &s_capture_task_handle,
        AUDIO_CAPTURE_TASK_CORE);
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
