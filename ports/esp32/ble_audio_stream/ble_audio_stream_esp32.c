#include "ble_audio_stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "listener_audio_proto.h"

#define BLE_AUDIO_STREAM_TASK_STACK_BYTES (6 * 1024)
#define BLE_AUDIO_STREAM_PACKET_MAX_BYTES 180
#define BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES (BLE_AUDIO_STREAM_PACKET_MAX_BYTES - LISTENER_AUDIO_PROTO_HEADER_BYTES)
#define BLE_AUDIO_STREAM_NOTIFY_QUEUE_LENGTH 2

typedef struct {
    ble_audio_stream_export_t export_info;
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
static QueueHandle_t s_export_queue;
static TaskHandle_t s_export_task_handle;

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
    uint16_t chunk_index,
    uint8_t fragment_index,
    uint8_t fragment_count,
    const uint8_t *payload,
    uint16_t payload_len,
    uint16_t chunk_pcm_bytes)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t packet[BLE_AUDIO_STREAM_PACKET_MAX_BYTES];
    listener_audio_packet_header_t header;
    listener_audio_proto_header_init(
        &header,
        packet_type,
        session_id,
        chunk_index,
        fragment_index,
        fragment_count,
        payload_len,
        chunk_pcm_bytes);

    memcpy(packet, &header, sizeof(header));
    if (payload_len > 0 && payload != NULL) {
        memcpy(packet + sizeof(header), payload, payload_len);
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, (uint16_t)(sizeof(header) + payload_len));
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_notify_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(
            TAG,
            "notify failed: type=%u session=%" PRIu32 " chunk=%u frag=%u/%u rc=%d",
            (unsigned)packet_type,
            session_id,
            chunk_index,
            fragment_index,
            fragment_count,
            rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void ble_audio_stream_free_job(ble_audio_stream_job_t *job)
{
    if (job == NULL) {
        return;
    }
    free(job->owned_pcm);
    memset(job, 0, sizeof(*job));
}

static esp_err_t ble_audio_stream_send_export_internal(const ble_audio_stream_export_t *export_info)
{
    if (export_info == NULL || export_info->pcm_buffer == NULL || export_info->pcm_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "audio session upload begin: session=%" PRIu32 " pcm_bytes=%u frame_bytes=%u chunk_pcm_bytes=%u ready=%u conn=%d",
        export_info->session_id,
        (unsigned)export_info->pcm_bytes,
        export_info->frame_bytes,
        export_info->chunk_pcm_bytes,
        ble_audio_stream_is_ready(),
        s_conn_handle);

    if (!ble_audio_stream_is_ready()) {
        ESP_LOGW(TAG, "audio session upload skipped: BLE notify not ready");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ble_audio_stream_send_packet(
        LISTENER_AUDIO_PACKET_TYPE_SESSION_START,
        export_info->session_id,
        0,
        0,
        1,
        NULL,
        0,
        0);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t chunk_pcm_bytes = export_info->chunk_pcm_bytes;
    uint16_t chunk_index = 0;
    for (size_t chunk_offset = 0; chunk_offset < export_info->pcm_bytes; chunk_offset += chunk_pcm_bytes, ++chunk_index) {
        uint16_t this_chunk_bytes = (uint16_t)(export_info->pcm_bytes - chunk_offset);
        if (this_chunk_bytes > chunk_pcm_bytes) {
            this_chunk_bytes = chunk_pcm_bytes;
        }

        uint8_t fragment_count = (uint8_t)((this_chunk_bytes + BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES - 1) / BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES);
        if (fragment_count == 0) {
            fragment_count = 1;
        }

        for (uint8_t fragment_index = 0; fragment_index < fragment_count; ++fragment_index) {
            size_t fragment_offset = chunk_offset + ((size_t)fragment_index * BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES);
            uint16_t payload_len = (uint16_t)(export_info->pcm_bytes - fragment_offset);
            if (payload_len > BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES) {
                payload_len = BLE_AUDIO_STREAM_PACKET_PAYLOAD_BYTES;
            }
            if (payload_len > (uint16_t)(chunk_offset + this_chunk_bytes - fragment_offset)) {
                payload_len = (uint16_t)(chunk_offset + this_chunk_bytes - fragment_offset);
            }

            err = ble_audio_stream_send_packet(
                LISTENER_AUDIO_PACKET_TYPE_AUDIO_CHUNK,
                export_info->session_id,
                chunk_index,
                fragment_index,
                fragment_count,
                export_info->pcm_buffer + fragment_offset,
                payload_len,
                this_chunk_bytes);
            if (err != ESP_OK) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(8));
        }

        if (err != ESP_OK) {
            break;
        }
    }

    if (err == ESP_OK) {
        err = ble_audio_stream_send_packet(
            LISTENER_AUDIO_PACKET_TYPE_SESSION_STOP,
            export_info->session_id,
            chunk_index,
            0,
            1,
            NULL,
            0,
            0);
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
        ble_audio_stream_send_export_internal(&job.export_info);

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

    BaseType_t task_ok = xTaskCreate(
        ble_audio_stream_task,
        "ble_audio_stream_task",
        BLE_AUDIO_STREAM_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 5,
        &s_export_task_handle);
    if (task_ok != pdPASS) {
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
    s_notify_enabled = false;
}

void ble_audio_stream_on_gap_disconnect(uint16_t conn_handle)
{
    if (s_conn_handle == conn_handle) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
    }
}

void ble_audio_stream_on_gap_subscribe(
    uint16_t conn_handle,
    uint16_t attr_handle,
    uint8_t cur_notify,
    uint8_t cur_indicate)
{
    (void)cur_indicate;
    if (attr_handle == s_notify_attr_handle && conn_handle == s_conn_handle) {
        s_notify_enabled = cur_notify != 0;
        ESP_LOGI(
            TAG,
            "audio notify subscription changed: conn=%d attr=%d notify=%u",
            conn_handle,
            attr_handle,
            cur_notify);
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
