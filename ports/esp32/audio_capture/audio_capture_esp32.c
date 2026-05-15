#include "audio_capture.h"
#include "audio_capture_platform.h"
#include "ble_audio_stream.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

#define AUDIO_CAPTURE_I2C_PORT          (0)
#define AUDIO_CAPTURE_I2C_SDA_IO        (4)
#define AUDIO_CAPTURE_I2C_SCL_IO        (5)

#define AUDIO_CAPTURE_I2S_PORT          (0)
#define AUDIO_CAPTURE_I2S_MCLK_IO       (45)
#define AUDIO_CAPTURE_I2S_BCLK_IO       (39)
#define AUDIO_CAPTURE_I2S_WS_IO         (41)
#define AUDIO_CAPTURE_I2S_DIN_IO        (40)

#define AUDIO_CAPTURE_SAMPLE_RATE_HZ    (16000)
#define AUDIO_CAPTURE_MCLK_MULTIPLE     (384)
#define AUDIO_CAPTURE_INPUT_GAIN_DB     (42.0f)
#define AUDIO_CAPTURE_FRAME_MS          (20)
#define AUDIO_CAPTURE_FRAME_SAMPLES     ((AUDIO_CAPTURE_SAMPLE_RATE_HZ * AUDIO_CAPTURE_FRAME_MS) / 1000)
#define AUDIO_CAPTURE_FRAME_BYTES       (AUDIO_CAPTURE_FRAME_SAMPLES * sizeof(int16_t))
#define AUDIO_CAPTURE_LOG_INTERVAL_FRAMES (50)
#define AUDIO_CAPTURE_SESSION_SAFETY_MAX_SECONDS (600)
#define AUDIO_CAPTURE_STREAM_BATCH_FRAMES 4
#define AUDIO_CAPTURE_STREAM_BATCH_BYTES (AUDIO_CAPTURE_STREAM_BATCH_FRAMES * AUDIO_CAPTURE_FRAME_BYTES)

static const char *TAG = "audio_capture";

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

static bool s_started;
static i2s_chan_handle_t s_i2s_rx_handle;
static i2c_master_bus_handle_t s_i2c_bus_handle;
static esp_codec_dev_handle_t s_codec_handle;
static TaskHandle_t s_capture_task_handle;
static uint32_t s_frame_captured_count;
static uint32_t s_overflow_count;
static uint32_t s_underrun_count;
static uint32_t s_dropped_frame_count;
static audio_capture_export_state_t s_export_state;
static SemaphoreHandle_t s_state_mutex;
static uint32_t s_session_id_counter;

static void audio_capture_export_cleanup(void)
{
    memset(&s_export_state, 0, sizeof(s_export_state));
}


esp_err_t audio_capture_session_begin(void)
{
    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_export_state.requested || s_export_state.active) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_export_state, 0, sizeof(s_export_state));
    s_export_state.mode = AUDIO_CAPTURE_EXPORT_MODE_SESSION;
    s_export_state.duration_seconds = 0;
    s_export_state.total_frames = (AUDIO_CAPTURE_SESSION_SAFETY_MAX_SECONDS * 1000U) / AUDIO_CAPTURE_FRAME_MS;
    s_export_state.pcm_bytes_total = 0;
    s_export_state.session_id = ++s_session_id_counter;

    s_export_state.requested = true;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(
        TAG,
        "record session begin requested: session_id=%" PRIu32 " buffer_ms=%u buffer_bytes=%u safety_max_s=%u",
        s_export_state.session_id,
        AUDIO_CAPTURE_STREAM_BATCH_FRAMES * AUDIO_CAPTURE_FRAME_MS,
        (unsigned)AUDIO_CAPTURE_STREAM_BATCH_BYTES,
        AUDIO_CAPTURE_SESSION_SAFETY_MAX_SECONDS);
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

i2c_master_bus_handle_t audio_capture_get_i2c_bus_handle(void)
{
    return s_i2c_bus_handle;
}

static void audio_capture_task(void *arg)
{
    int16_t frame_buffer[AUDIO_CAPTURE_FRAME_SAMPLES];

    while (1) {
        int ret = esp_codec_dev_read(s_codec_handle, frame_buffer, sizeof(frame_buffer));
        if (ret == ESP_CODEC_DEV_OK) {
            s_frame_captured_count++;

            bool should_emit = false;
            bool should_cancel = false;
            bool idle_for_logging = false;
            bool should_session_start = false;
            bool should_session_audio = false;
            bool should_session_stop = false;
            bool should_session_cancel = false;
            uint32_t session_id = 0;
            uint16_t packet_sequence_start = 0;
            uint16_t batch_pcm_bytes = 0;
            uint16_t expected_packet_count_at_end = 0;
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
                    if (s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION) {
                        if (!s_export_state.ble_session_started) {
                            should_session_start = true;
                            session_id = s_export_state.session_id;
                            s_export_state.ble_session_started = true;
                        }

                        size_t batch_offset =
                            (size_t)s_export_state.stream_batch_frame_count * AUDIO_CAPTURE_FRAME_BYTES;
                        memcpy(s_export_state.stream_batch_buffer + batch_offset, frame_buffer, AUDIO_CAPTURE_FRAME_BYTES);
                        s_export_state.stream_batch_frame_count++;
                        s_export_state.pcm_bytes_written += AUDIO_CAPTURE_FRAME_BYTES;

                        if (s_export_state.stream_batch_frame_count >= AUDIO_CAPTURE_STREAM_BATCH_FRAMES) {
                            should_session_audio = true;
                            session_id = s_export_state.session_id;
                            packet_sequence_start = s_export_state.stream_next_packet_sequence;
                            batch_pcm_bytes = AUDIO_CAPTURE_STREAM_BATCH_BYTES;
                            memcpy(s_export_state.stream_emit_buffer, s_export_state.stream_batch_buffer, batch_pcm_bytes);
                            audio_batch_copy = s_export_state.stream_emit_buffer;
                            packet_count = ble_audio_stream_count_audio_packets(batch_pcm_bytes);
                            s_export_state.stream_next_packet_sequence =
                                (uint16_t)(s_export_state.stream_next_packet_sequence + packet_count);
                            s_export_state.stream_batch_frame_count = 0;
                        }
                    }

                    s_export_state.captured_frames++;

                    if (s_export_state.mode == AUDIO_CAPTURE_EXPORT_MODE_SESSION &&
                        (s_export_state.stop_requested || s_export_state.captured_frames >= s_export_state.total_frames)) {
                        if (s_export_state.captured_frames >= s_export_state.total_frames) {
                            ESP_LOGW(
                                TAG,
                                "record session hit safety max duration: max_duration_s=%" PRIu32,
                                AUDIO_CAPTURE_SESSION_SAFETY_MAX_SECONDS);
                        }
                        s_export_state.duration_seconds =
                            (s_export_state.captured_frames * AUDIO_CAPTURE_FRAME_MS) / 1000U;

                        if (s_export_state.stream_batch_frame_count > 0) {
                            should_session_audio = true;
                            session_id = s_export_state.session_id;
                            packet_sequence_start = s_export_state.stream_next_packet_sequence;
                            batch_pcm_bytes =
                                s_export_state.stream_batch_frame_count * AUDIO_CAPTURE_FRAME_BYTES;
                            memcpy(s_export_state.stream_emit_buffer, s_export_state.stream_batch_buffer, batch_pcm_bytes);
                            audio_batch_copy = s_export_state.stream_emit_buffer;
                            packet_count = ble_audio_stream_count_audio_packets(batch_pcm_bytes);
                            s_export_state.stream_next_packet_sequence =
                                (uint16_t)(s_export_state.stream_next_packet_sequence + packet_count);
                            s_export_state.stream_batch_frame_count = 0;
                        }
                        should_session_stop = true;
                        session_id = s_export_state.session_id;
                        expected_packet_count_at_end = s_export_state.stream_next_packet_sequence;
                        should_emit = true;
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
                        ESP_LOGW(TAG, "stream session start failed: session_id=%" PRIu32 " ret=%s", session_id, esp_err_to_name(start_ret));
                    } else {
                        ESP_LOGI(TAG, "stream session start queued: session_id=%" PRIu32, session_id);
                    }
                }

                if (should_session_audio) {
                    esp_err_t audio_ret =
                        ble_audio_stream_send_session_audio(session_id, packet_sequence_start, audio_batch_copy, batch_pcm_bytes);
                    if (audio_ret != ESP_OK) {
                        ESP_LOGW(
                            TAG,
                            "stream audio packet batch failed: session_id=%" PRIu32 " seq_start=%u packet_count=%u pcm_bytes=%u ret=%s",
                            session_id,
                            packet_sequence_start,
                            packet_count,
                            batch_pcm_bytes,
                            esp_err_to_name(audio_ret));
                    } else {
                        ESP_LOGI(
                            TAG,
                            "stream audio packet batch queued: session_id=%" PRIu32 " seq_start=%u packet_count=%u pcm_bytes=%u",
                            session_id,
                            packet_sequence_start,
                            packet_count,
                            batch_pcm_bytes);
                    }
                }

                if (should_session_stop) {
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

                if (should_emit) {
                    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
                        audio_capture_export_cleanup();
                        xSemaphoreGive(s_state_mutex);
                    }
                }
            }
            if (idle_for_logging && (s_frame_captured_count % AUDIO_CAPTURE_LOG_INTERVAL_FRAMES) == 0) {
                ESP_LOGI(
                    TAG,
                    "frame captured count=%" PRIu32 " bytes=%" PRIu32,
                    s_frame_captured_count,
                    s_frame_captured_count * (uint32_t)AUDIO_CAPTURE_FRAME_BYTES);
            }
            continue;
        }

        if (ret == ESP_CODEC_DEV_WRONG_STATE) {
            s_underrun_count++;
            ESP_LOGW(TAG, "underrun count=%" PRIu32 " ret=%d", s_underrun_count, ret);
        } else if (ret == ESP_CODEC_DEV_DRV_ERR || ret == ESP_CODEC_DEV_READ_FAIL) {
            s_overflow_count++;
            s_dropped_frame_count++;
            ESP_LOGW(
                TAG,
                "overflow count=%" PRIu32 " dropped frame=%" PRIu32 " ret=%d",
                s_overflow_count,
                s_dropped_frame_count,
                ret);
        } else {
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "dropped frame=%" PRIu32 " ret=%d", s_dropped_frame_count, ret);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static esp_err_t audio_capture_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_CAPTURE_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx_handle), TAG, "create i2s channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = AUDIO_CAPTURE_I2S_MCLK_IO,
            .bclk = AUDIO_CAPTURE_I2S_BCLK_IO,
            .ws = AUDIO_CAPTURE_I2S_WS_IO,
            .dout = -1,
            .din = AUDIO_CAPTURE_I2S_DIN_IO,
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
        .port = AUDIO_CAPTURE_I2S_PORT,
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

esp_err_t audio_capture_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t err = audio_capture_i2s_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s start fail");
        return err;
    }
    ESP_LOGI(TAG, "i2s start ok");

    err = audio_capture_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c init fail");
        i2s_channel_disable(s_i2s_rx_handle);
        return err;
    }

    err = audio_capture_codec_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec init fail");
        i2s_channel_disable(s_i2s_rx_handle);
        return err;
    }
    ESP_LOGI(TAG, "codec init ok");

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
        4096,
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
    return ESP_OK;
}

bool audio_capture_is_running(void)
{
    return s_started && s_capture_task_handle != NULL;
}
