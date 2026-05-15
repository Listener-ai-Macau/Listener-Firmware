#include "ble_audio_stream.h"

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

#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "listener_audio_proto.h"

#define BLE_AUDIO_STREAM_TASK_STACK_BYTES (6 * 1024)
#define BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES 244
#define BLE_AUDIO_STREAM_PACKET_MAX_BYTES 500
#define BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH 12
#define BLE_AUDIO_STREAM_NOTIFY_WINDOW_DEPTH 6
#define BLE_AUDIO_STREAM_NOTIFY_WAIT_MS 1000
#define BLE_AUDIO_STREAM_NOTIFY_TX_DELAY_MS 2
#define BLE_AUDIO_STREAM_MIN_REQUIRED_MBUF 2
#define BLE_AUDIO_STREAM_NOTIFY_RETRY_LIMIT 80

typedef enum {
    BLE_AUDIO_STREAM_JOB_TYPE_EXPORT = 0,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP,
    BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL,
} ble_audio_stream_job_type_t;

typedef struct {
    ble_audio_stream_job_type_t type;
    ble_audio_stream_export_t export_info;
    uint32_t session_id;
    uint16_t packet_sequence;
    uint16_t pcm_bytes;
    uint16_t expected_packet_count;
    uint8_t *owned_pcm;
} ble_audio_stream_job_t;

static const char *TAG = "ble_audio_stream";

static const ble_uuid128_t s_service_uuid = BLE_AUDIO_STREAM_SERVICE_UUID;
static const ble_uuid128_t s_notify_uuid = BLE_AUDIO_STREAM_NOTIFY_UUID;

static uint16_t s_notify_attr_handle;
static bool s_started;
static bool s_registered;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_notify_enabled;
static bool s_pending_subscribe_valid;
static uint16_t s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_pending_notify_enabled;
static uint16_t s_packet_value_max_bytes = BLE_AUDIO_STREAM_PACKET_DEFAULT_BYTES;
static QueueHandle_t s_export_queue;
static TaskHandle_t s_export_task_handle;
static SemaphoreHandle_t s_notify_credit_sem;
static uint8_t s_notify_window_depth = BLE_AUDIO_STREAM_NOTIFY_WINDOW_DEPTH;

_Static_assert(
    LISTENER_AUDIO_PROTO_HEADER_BYTES <= sizeof(listener_audio_packet_header_t),
    "listener audio header constant exceeds struct size");

static uint16_t ble_audio_stream_packet_payload_bytes(void)
{
    if (s_packet_value_max_bytes <= LISTENER_AUDIO_PROTO_HEADER_BYTES) {
        return 0;
    }
    return (uint16_t)(s_packet_value_max_bytes - LISTENER_AUDIO_PROTO_HEADER_BYTES);
}

uint16_t ble_audio_stream_get_audio_payload_bytes(void)
{
    return (uint16_t)(ble_audio_stream_packet_payload_bytes() & ~1u);
}

uint16_t ble_audio_stream_count_audio_packets(uint16_t pcm_bytes)
{
    uint16_t payload_bytes = ble_audio_stream_get_audio_payload_bytes();
    if (pcm_bytes == 0 || payload_bytes == 0) {
        return 0;
    }

    return (uint16_t)((pcm_bytes + payload_bytes - 1u) / payload_bytes);
}

static void ble_audio_stream_reset_notify_credit(void)
{
    if (s_notify_credit_sem == NULL) {
        return;
    }

    xQueueReset(s_notify_credit_sem);
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
    s_notify_enabled = notify_enabled;
    if (s_notify_enabled) {
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

static int ble_audio_stream_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
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
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t packet_len = (uint16_t)(LISTENER_AUDIO_PROTO_HEADER_BYTES + payload_len);
    if (packet_len > s_packet_value_max_bytes || packet_len > BLE_AUDIO_STREAM_PACKET_MAX_BYTES) {
        ESP_LOGW(
            TAG,
            "notify packet too large: type=%u session=%" PRIu32 " seq_or_count=%u len=%u max=%u",
            (unsigned)packet_type,
            session_id,
            sequence_or_count,
            packet_len,
            s_packet_value_max_bytes);
        return ESP_ERR_INVALID_SIZE;
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
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) {
            ESP_LOGW(TAG, "notify aborted: link lost during retry");
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t credit_err = ble_audio_stream_wait_notify_credit(
            packet_type,
            session_id,
            sequence_or_count,
            fragment_index,
            fragment_count);
        if (credit_err != ESP_OK) {
            return credit_err;
        }

        if (os_msys_num_free() < BLE_AUDIO_STREAM_MIN_REQUIRED_MBUF) {
            xSemaphoreGive(s_notify_credit_sem);
            vTaskDelay(pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_TX_DELAY_MS));
            continue;
        }

        struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
        if (om == NULL) {
            xSemaphoreGive(s_notify_credit_sem);
            vTaskDelay(pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_TX_DELAY_MS));
            continue;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_notify_attr_handle, om);
        if (rc == 0) {
            return ESP_OK;
        }

        os_mbuf_free_chain(om);
        if (rc == BLE_HS_ENOMEM) {
            xSemaphoreGive(s_notify_credit_sem);
            vTaskDelay(pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_TX_DELAY_MS));
            continue;
        }

        if (s_notify_credit_sem != NULL) {
            xSemaphoreGive(s_notify_credit_sem);
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
        return ESP_FAIL;
    }

    ESP_LOGW(
        TAG,
        "notify failed after retries: type=%u session=%" PRIu32 " seq_or_count=%u frag=%u/%u",
        (unsigned)packet_type,
        session_id,
        sequence_or_count,
        fragment_index,
        fragment_count);
    return ESP_ERR_TIMEOUT;
}

static void ble_audio_stream_free_job(ble_audio_stream_job_t *job)
{
    if (job == NULL) {
        return;
    }
    free(job->owned_pcm);
    memset(job, 0, sizeof(*job));
}

static esp_err_t ble_audio_stream_send_session_start_internal(uint32_t session_id)
{
    return ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_START,
        session_id,
        0,
        0,
        1,
        NULL,
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

    uint16_t payload_bytes = ble_audio_stream_get_audio_payload_bytes();
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
        vTaskDelay(pdMS_TO_TICKS(BLE_AUDIO_STREAM_NOTIFY_TX_DELAY_MS));
    }

    if (dropped_packet_count > 0) {
        ESP_LOGW(
            TAG,
            "audio data batch completed with drops: session=%" PRIu32 " seq_start=%u packet_count=%u sent=%u dropped=%u last_ret=%s",
            session_id,
            packet_sequence,
            packet_count,
            packet_count - dropped_packet_count,
            dropped_packet_count,
            esp_err_to_name(last_packet_error));
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
    return ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        0);
}

static esp_err_t ble_audio_stream_send_session_cancel_internal(uint32_t session_id, uint16_t expected_packet_count)
{
    ESP_LOGI(
        TAG,
        "audio session cancel: session=%" PRIu32 " expected_packet_count=%u",
        session_id,
        expected_packet_count);
    return ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_CANCEL,
        session_id,
        expected_packet_count,
        0,
        1,
        NULL,
        0,
        0);
}

static esp_err_t ble_audio_stream_send_export_internal(const ble_audio_stream_export_t *export_info)
{
    if (export_info == NULL || export_info->pcm_buffer == NULL || export_info->pcm_bytes == 0 ||
        export_info->chunk_pcm_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "audio session upload begin: session=%" PRIu32 " pcm_bytes=%u frame_bytes=%u batch_pcm_bytes=%u payload_max=%u ready=%u conn=%d",
        export_info->session_id,
        (unsigned)export_info->pcm_bytes,
        export_info->frame_bytes,
        export_info->chunk_pcm_bytes,
        ble_audio_stream_get_audio_payload_bytes(),
        ble_audio_stream_is_ready(),
        s_conn_handle);

    if (!ble_audio_stream_is_ready()) {
        ESP_LOGW(TAG, "audio session upload skipped: BLE notify not ready");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ble_audio_stream_send_session_start_internal(export_info->session_id);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t batch_pcm_bytes = export_info->chunk_pcm_bytes;
    uint16_t packet_sequence = 0;
    for (size_t batch_offset = 0; batch_offset < export_info->pcm_bytes; batch_offset += batch_pcm_bytes) {
        uint16_t this_batch_bytes = (uint16_t)(export_info->pcm_bytes - batch_offset);
        if (this_batch_bytes > batch_pcm_bytes) {
            this_batch_bytes = batch_pcm_bytes;
        }

        uint16_t batch_packet_count = ble_audio_stream_count_audio_packets(this_batch_bytes);
        if (batch_packet_count == 0) {
            return ESP_ERR_INVALID_SIZE;
        }

        if ((uint32_t)packet_sequence + batch_packet_count > UINT16_MAX) {
            ESP_LOGW(
                TAG,
                "audio session export packet sequence overflow: session=%" PRIu32 " seq=%u add=%u",
                export_info->session_id,
                packet_sequence,
                batch_packet_count);
            return ESP_ERR_INVALID_SIZE;
        }

        err = ble_audio_stream_send_session_audio_internal(
            export_info->session_id,
            packet_sequence,
            export_info->pcm_buffer + batch_offset,
            this_batch_bytes);
        if (err != ESP_OK) {
            break;
        }

        packet_sequence = (uint16_t)(packet_sequence + batch_packet_count);
    }

    if (err == ESP_OK) {
        err = ble_audio_stream_send_session_stop_internal(export_info->session_id, packet_sequence);
    }

    ESP_LOGI(
        TAG,
        "audio session upload end: session=%" PRIu32 " status=%s",
        export_info->session_id,
        esp_err_to_name(err));
    return err;
}

static void ble_audio_stream_task(void *parameter)
{
    (void)parameter;

    ble_audio_stream_job_t job;
    while (1) {
        if (xQueueReceive(s_export_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (job.type) {
            case BLE_AUDIO_STREAM_JOB_TYPE_EXPORT:
                ble_audio_stream_send_export_internal(&job.export_info);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START:
                ble_audio_stream_send_session_start_internal(job.session_id);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA:
                ble_audio_stream_send_session_audio_internal(
                    job.session_id,
                    job.packet_sequence,
                    job.owned_pcm,
                    job.pcm_bytes);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP:
                ble_audio_stream_send_session_stop_internal(job.session_id, job.expected_packet_count);
                break;
            case BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL:
                ble_audio_stream_send_session_cancel_internal(job.session_id, job.expected_packet_count);
                break;
            default:
                break;
        }

        ble_audio_stream_free_job(&job);
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

    BaseType_t task_ok = xTaskCreate(
        ble_audio_stream_task,
        "ble_audio_stream_task",
        BLE_AUDIO_STREAM_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 5,
        &s_export_task_handle);
    if (task_ok != pdPASS) {
        vSemaphoreDelete(s_notify_credit_sem);
        s_notify_credit_sem = NULL;
        vQueueDelete(s_export_queue);
        s_export_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t ble_audio_stream_start(void)
{
    return ble_audio_stream_init();
}

void ble_audio_stream_on_gap_connect(uint16_t conn_handle)
{
    s_conn_handle = conn_handle;
    if (s_pending_subscribe_valid && s_pending_subscribe_conn_handle == conn_handle) {
        ble_audio_stream_apply_notify_enabled(s_pending_notify_enabled);
        ESP_LOGI(
            TAG,
            "audio notify subscription restored before connect: conn=%d notify=%u window=%u",
            conn_handle,
            s_pending_notify_enabled ? 1u : 0u,
            s_notify_window_depth);
        if (s_pending_notify_enabled) {
            ble_audio_stream_log_packet_size(conn_handle);
        }
        s_pending_subscribe_valid = false;
        s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        return;
    }

    ble_audio_stream_apply_notify_enabled(false);
}

void ble_audio_stream_on_gap_disconnect(uint16_t conn_handle)
{
    if (s_conn_handle == conn_handle) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_audio_stream_apply_notify_enabled(false);
    }

    if (s_pending_subscribe_valid && s_pending_subscribe_conn_handle == conn_handle) {
        s_pending_subscribe_valid = false;
        s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_pending_notify_enabled = false;
    }
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

    if (conn_handle != s_conn_handle) {
        s_pending_subscribe_valid = true;
        s_pending_subscribe_conn_handle = conn_handle;
        s_pending_notify_enabled = cur_notify != 0;
        ESP_LOGI(
            TAG,
            "audio notify subscription deferred until connect: conn=%d attr=%d notify=%u",
            conn_handle,
            attr_handle,
            cur_notify);
        return;
    }

    s_pending_subscribe_valid = false;
    s_pending_subscribe_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_pending_notify_enabled = false;
    ble_audio_stream_apply_notify_enabled(cur_notify != 0);
    ESP_LOGI(
        TAG,
        "audio notify subscription changed: conn=%d attr=%d notify=%u window=%u",
        conn_handle,
        attr_handle,
        cur_notify,
        s_notify_window_depth);
    if (cur_notify != 0) {
        ble_audio_stream_log_packet_size(conn_handle);
    }
}

void ble_audio_stream_on_gap_mtu(uint16_t conn_handle, uint16_t mtu)
{
    if (conn_handle != s_conn_handle || mtu <= 3) {
        return;
    }

    uint16_t value_max = (uint16_t)(mtu - 3);
    if (value_max > BLE_AUDIO_STREAM_PACKET_MAX_BYTES) {
        value_max = BLE_AUDIO_STREAM_PACKET_MAX_BYTES;
    }
    if (value_max < LISTENER_AUDIO_PROTO_HEADER_BYTES) {
        value_max = LISTENER_AUDIO_PROTO_HEADER_BYTES;
    }

    s_packet_value_max_bytes = value_max;
    ESP_LOGI(TAG, "audio notify mtu updated: conn=%u mtu=%u", conn_handle, mtu);
    ble_audio_stream_log_packet_size(conn_handle);
}

void ble_audio_stream_on_gap_notify_tx(
    uint16_t conn_handle,
    uint16_t attr_handle,
    int status,
    bool indication)
{
    (void)indication;

    if (conn_handle != s_conn_handle || attr_handle != s_notify_attr_handle || !s_notify_enabled) {
        return;
    }

    if (status == 0 || status == BLE_HS_EDONE) {
        if (s_notify_credit_sem != NULL) {
            xSemaphoreGive(s_notify_credit_sem);
        }
        return;
    }

    ESP_LOGW(
        TAG,
        "notify tx completion error: conn=%u attr=%u status=%d",
        conn_handle,
        attr_handle,
        status);
    if (s_notify_credit_sem != NULL) {
        xSemaphoreGive(s_notify_credit_sem);
    }
}

bool ble_audio_stream_is_ready(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_notify_enabled;
}

uint16_t ble_audio_stream_get_notify_attr_handle(void)
{
    return s_notify_attr_handle;
}

esp_err_t ble_audio_stream_send_export(const ble_audio_stream_export_t *export_info)
{
    if (!s_started || s_export_queue == NULL || export_info == NULL || export_info->pcm_buffer == NULL || export_info->pcm_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ble_audio_stream_job_t job = {0};
    job.type = BLE_AUDIO_STREAM_JOB_TYPE_EXPORT;
    job.owned_pcm = (uint8_t *)malloc(export_info->pcm_bytes);
    if (job.owned_pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(job.owned_pcm, export_info->pcm_buffer, export_info->pcm_bytes);
    job.export_info = *export_info;
    job.export_info.pcm_buffer = job.owned_pcm;

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        free(job.owned_pcm);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t ble_audio_stream_send_export_blocking(const ble_audio_stream_export_t *export_info)
{
    return ble_audio_stream_send_export_internal(export_info);
}

esp_err_t ble_audio_stream_send_session_start(uint32_t session_id)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_START,
        .session_id = session_id,
    };

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
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
    if (packet_count == 0 || (uint32_t)packet_sequence + packet_count > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_AUDIO_DATA,
        .session_id = session_id,
        .packet_sequence = packet_sequence,
        .pcm_bytes = pcm_bytes,
    };
    job.owned_pcm = (uint8_t *)malloc(pcm_bytes);
    if (job.owned_pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(job.owned_pcm, pcm_buffer, pcm_bytes);

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        free(job.owned_pcm);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_stop(uint32_t session_id, uint16_t expected_packet_count)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_STOP,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
    };

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ble_audio_stream_send_session_cancel(uint32_t session_id, uint16_t expected_packet_count)
{
    if (!s_started || s_export_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_audio_stream_job_t job = {
        .type = BLE_AUDIO_STREAM_JOB_TYPE_SESSION_CANCEL,
        .session_id = session_id,
        .expected_packet_count = expected_packet_count,
    };

    if (xQueueSend(s_export_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
