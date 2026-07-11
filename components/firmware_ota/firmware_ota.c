#include "firmware_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "audio_capture.h"
#include "ble_audio_stream.h"
#include "diag_log.h"
#include "esp_app_desc.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "listener_device.h"
#include "nvs.h"
#include "power_manager.h"
#include "status_led.h"

extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));

static const char *TAG = "firmware_ota";

#define FIRMWARE_OTA_USB_PREFIX "OTA:"
#define FIRMWARE_OTA_VERSION_BYTES 32
#define FIRMWARE_OTA_MIN_BATTERY_PERCENT 20
#define FIRMWARE_OTA_RESUME_NVS_NAMESPACE "ota_resume"
#define FIRMWARE_OTA_RESUME_NVS_KEY "session_v1"
#define FIRMWARE_OTA_RESUME_SCHEMA 1U
#define FIRMWARE_OTA_RESUME_SHA256_BYTES 32U
#define FIRMWARE_OTA_RESUME_CHECKPOINT_BYTES (64U * 1024U)

typedef struct {
    SemaphoreHandle_t mutex;
    esp_ota_handle_t handle;
    const esp_partition_t *running_partition;
    const esp_partition_t *boot_partition;
    const esp_partition_t *update_partition;
    size_t expected_size;
    size_t bytes_written;
    bool active;
    bool post_ok;
    bool ble_ready;
    bool keyboard_ready;
    bool battery_valid;
    uint8_t battery_percent;
    uint32_t battery_mv;
    char target_version[FIRMWARE_OTA_VERSION_BYTES];
    uint8_t resume_sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES];
    bool resume_identity_valid;
    size_t resume_checkpoint_bytes;
} firmware_ota_ctx_t;

typedef struct {
    uint32_t schema;
    uint32_t expected_size;
    uint32_t bytes_written;
    uint32_t partition_offset;
    uint8_t sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES];
} firmware_ota_resume_record_t;

static firmware_ota_ctx_t s_ota;

static void firmware_ota_lock(void)
{
    if (s_ota.mutex != NULL) {
        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    }
}

static void firmware_ota_unlock(void)
{
    if (s_ota.mutex != NULL) {
        xSemaphoreGive(s_ota.mutex);
    }
}

static uint32_t partition_subtype_u32(const esp_partition_t *partition)
{
    return partition != NULL ? (uint32_t)partition->subtype : UINT32_MAX;
}

static uint32_t partition_offset_u32(const esp_partition_t *partition)
{
    return partition != NULL ? partition->address : UINT32_MAX;
}

static uint32_t partition_size_u32(const esp_partition_t *partition)
{
    return partition != NULL ? partition->size : 0;
}

static uint32_t firmware_ota_string_hash(const char *text)
{
    uint32_t hash = 2166136261U;
    if (text == NULL) {
        text = "unknown";
    }
    while (*text != '\0') {
        hash ^= (uint8_t)*text++;
        hash *= 16777619U;
    }
    return hash;
}

static const char *partition_label_or_none(const esp_partition_t *partition)
{
    return partition != NULL ? partition->label : "none";
}

static uint32_t ota_state_for_partition(const esp_partition_t *partition)
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (partition == NULL || esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        return (uint32_t)ESP_OTA_IMG_UNDEFINED;
    }
    return (uint32_t)state;
}

static bool firmware_ota_running_pending_verify(void)
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t *running = esp_ota_get_running_partition();
    return running != NULL &&
           esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

static void firmware_ota_log_event(
    uint8_t event,
    uint8_t severity,
    const esp_partition_t *partition,
    uint32_t detail,
    uint32_t error,
    uint32_t reason)
{
    diag_log(DIAG_SRC_OTA, event, severity,
             partition_subtype_u32(partition), detail, error, reason);
}

static void firmware_ota_log_partition_event(uint32_t role, const esp_partition_t *partition)
{
    diag_log(DIAG_SRC_OTA, DIAG_OTA_PARTITION, partition != NULL ? DIAG_SEV_INFO : DIAG_SEV_WARN,
             role,
             partition_subtype_u32(partition),
             partition_offset_u32(partition),
             partition_size_u32(partition));
}

static void firmware_ota_log_version_values(
    uint8_t event,
    const esp_partition_t *partition,
    const char *from_version,
    const char *to_version)
{
    diag_log(DIAG_SRC_OTA, DIAG_OTA_VERSION, DIAG_SEV_INFO,
             firmware_ota_string_hash(from_version),
             firmware_ota_string_hash(to_version),
             partition_subtype_u32(partition),
             event);
}

static void firmware_ota_log_version_event(uint8_t event, const esp_partition_t *partition)
{
    const char *target = s_ota.target_version[0] != '\0' ? s_ota.target_version : "unknown";
    firmware_ota_log_version_values(event, partition, listener_device_get_fw_version(), target);
}

static void firmware_ota_reset_session_locked(void)
{
    s_ota.handle = 0;
    s_ota.update_partition = NULL;
    s_ota.expected_size = 0;
    s_ota.bytes_written = 0;
    s_ota.active = false;
    s_ota.target_version[0] = '\0';
    memset(s_ota.resume_sha256, 0, sizeof(s_ota.resume_sha256));
    s_ota.resume_identity_valid = false;
    s_ota.resume_checkpoint_bytes = 0;
}

static void firmware_ota_resume_clear_journal(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        FIRMWARE_OTA_RESUME_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA resume journal open for clear failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_erase_key(nvs, FIRMWARE_OTA_RESUME_NVS_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA resume journal clear failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t firmware_ota_resume_store_journal(
    const esp_partition_t *partition,
    size_t expected_size,
    size_t bytes_written,
    const uint8_t sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES])
{
    if (partition == NULL || sha256 == NULL || expected_size == 0 ||
        expected_size > UINT32_MAX || bytes_written > expected_size) {
        return ESP_ERR_INVALID_ARG;
    }

    firmware_ota_resume_record_t record = {
        .schema = FIRMWARE_OTA_RESUME_SCHEMA,
        .expected_size = (uint32_t)expected_size,
        .bytes_written = (uint32_t)bytes_written,
        .partition_offset = partition->address,
    };
    memcpy(record.sha256, sha256, sizeof(record.sha256));

    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        FIRMWARE_OTA_RESUME_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(nvs, FIRMWARE_OTA_RESUME_NVS_KEY, &record, sizeof(record));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

static esp_err_t firmware_ota_resume_load_journal(firmware_ota_resume_record_t *out_record)
{
    if (out_record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(
        FIRMWARE_OTA_RESUME_NVS_NAMESPACE,
        NVS_READONLY,
        &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t length = sizeof(*out_record);
    ret = nvs_get_blob(nvs, FIRMWARE_OTA_RESUME_NVS_KEY, out_record, &length);
    nvs_close(nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    return length == sizeof(*out_record) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool firmware_ota_resume_record_matches(
    const firmware_ota_resume_record_t *record,
    const esp_partition_t *partition,
    size_t image_size,
    const uint8_t sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES])
{
    return record != NULL && partition != NULL && sha256 != NULL &&
           record->schema == FIRMWARE_OTA_RESUME_SCHEMA &&
           record->expected_size == image_size &&
           record->bytes_written <= record->expected_size &&
           record->partition_offset == partition->address &&
           memcmp(record->sha256, sha256, sizeof(record->sha256)) == 0;
}

static void firmware_ota_request_active_ble_connection(const char *reason)
{
    if (ble_hid_gap_request_active_connection == NULL) {
        return;
    }

    esp_err_t ret = ble_hid_gap_request_active_connection();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA active BLE connection request failed reason=%s ret=%s",
                 reason != NULL ? reason : "ota", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "OTA requested active BLE connection parameters reason=%s",
                 reason != NULL ? reason : "ota");
    }
}

static void firmware_ota_set_runtime_active(bool active, size_t bytes_written, size_t expected_size, const char *reason)
{
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_OTA, active);
    if (active && reason != NULL) {
        firmware_ota_request_active_ble_connection(reason);
    }
    status_led_set_ota_active(active, bytes_written, expected_size, reason);
}

const char *firmware_ota_blocker_name(firmware_ota_blocker_t blocker)
{
    switch (blocker) {
    case FIRMWARE_OTA_BLOCKER_NONE: return "none";
    case FIRMWARE_OTA_BLOCKER_IN_PROGRESS: return "ota_in_progress";
    case FIRMWARE_OTA_BLOCKER_PENDING_VERIFY: return "pending_verify";
    case FIRMWARE_OTA_BLOCKER_RECORDING_ACTIVE: return "recording_active";
    case FIRMWARE_OTA_BLOCKER_BLE_AUDIO_ACTIVE: return "ble_audio_active";
    case FIRMWARE_OTA_BLOCKER_DIAG_EXPORT_ACTIVE: return "diag_export_active";
    case FIRMWARE_OTA_BLOCKER_LOW_BATTERY: return "low_battery";
    case FIRMWARE_OTA_BLOCKER_NO_PARTITION: return "no_update_partition";
    default: return "unknown";
    }
}

firmware_ota_blocker_t firmware_ota_get_blocker(void)
{
    firmware_ota_blocker_t blocker = FIRMWARE_OTA_BLOCKER_NONE;
    firmware_ota_lock();

    if (s_ota.active) {
        blocker = FIRMWARE_OTA_BLOCKER_IN_PROGRESS;
    } else if (firmware_ota_running_pending_verify()) {
        blocker = FIRMWARE_OTA_BLOCKER_PENDING_VERIFY;
    } else if (audio_capture_session_is_active()) {
        blocker = FIRMWARE_OTA_BLOCKER_RECORDING_ACTIVE;
    } else if (ble_audio_stream_is_busy()) {
        blocker = FIRMWARE_OTA_BLOCKER_BLE_AUDIO_ACTIVE;
    } else if (diag_log_is_dumping()) {
        blocker = FIRMWARE_OTA_BLOCKER_DIAG_EXPORT_ACTIVE;
    } else if (s_ota.battery_valid && s_ota.battery_percent < FIRMWARE_OTA_MIN_BATTERY_PERCENT) {
        blocker = FIRMWARE_OTA_BLOCKER_LOW_BATTERY;
    } else if (esp_ota_get_next_update_partition(NULL) == NULL) {
        blocker = FIRMWARE_OTA_BLOCKER_NO_PARTITION;
    }

    firmware_ota_unlock();
    return blocker;
}

void firmware_ota_init(void)
{
    if (s_ota.mutex == NULL) {
        s_ota.mutex = xSemaphoreCreateMutex();
    }

    firmware_ota_lock();
    s_ota.running_partition = esp_ota_get_running_partition();
    s_ota.boot_partition = esp_ota_get_boot_partition();
    const esp_partition_t *next_partition = esp_ota_get_next_update_partition(NULL);
    firmware_ota_unlock();

    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_RUNNING, s_ota.running_partition);
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_BOOT, s_ota.boot_partition);
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_NEXT, next_partition);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (s_ota.running_partition != NULL &&
        esp_ota_get_state_partition(s_ota.running_partition, &state) == ESP_OK) {
        ESP_LOGI(
            TAG,
            "OTA running partition=%s offset=0x%08" PRIx32 " boot=%s boot_offset=0x%08" PRIx32 " next=%s next_offset=0x%08" PRIx32 " state=%" PRIu32 " version=%s",
            partition_label_or_none(s_ota.running_partition),
            partition_offset_u32(s_ota.running_partition),
            partition_label_or_none(s_ota.boot_partition),
            partition_offset_u32(s_ota.boot_partition),
            partition_label_or_none(next_partition),
            partition_offset_u32(next_partition),
            (uint32_t)state,
            listener_device_get_fw_version());
        firmware_ota_log_event(
            state == ESP_OTA_IMG_PENDING_VERIFY ? DIAG_OTA_PENDING_VERIFY : DIAG_OTA_STATE,
            DIAG_SEV_INFO,
            s_ota.running_partition,
            (uint32_t)state,
            0,
            0);
    } else {
        ESP_LOGW(TAG, "OTA running partition state unavailable");
    }
}

void firmware_ota_record_self_check(bool post_ok, bool ble_ready, bool keyboard_ready)
{
    firmware_ota_lock();
    s_ota.post_ok = post_ok;
    s_ota.ble_ready = ble_ready;
    s_ota.keyboard_ready = keyboard_ready;
    firmware_ota_unlock();
}

esp_err_t firmware_ota_confirm_pending_verify_if_ready(void)
{
    if (!firmware_ota_running_pending_verify()) {
        return ESP_OK;
    }

    firmware_ota_lock();
    bool ready = s_ota.post_ok && s_ota.ble_ready && s_ota.keyboard_ready;
    bool post_ok = s_ota.post_ok;
    bool ble_ready = s_ota.ble_ready;
    bool keyboard_ready = s_ota.keyboard_ready;
    const esp_partition_t *running = s_ota.running_partition != NULL ?
        s_ota.running_partition : esp_ota_get_running_partition();
    firmware_ota_unlock();

    if (!ready) {
        uint32_t reason = 0;
        if (!post_ok) {
            reason |= DIAG_OTA_ROLLBACK_POST_FAILED;
        }
        if (!ble_ready) {
            reason |= DIAG_OTA_ROLLBACK_BLE_NOT_READY;
        }
        if (!keyboard_ready) {
            reason |= DIAG_OTA_ROLLBACK_KEYBOARD_NOT_READY;
        }
        ESP_LOGE(TAG, "OTA pending verify self-check failed; rolling back reason=0x%08" PRIx32, reason);
        firmware_ota_log_version_event(DIAG_OTA_ROLLBACK, running);
        firmware_ota_log_event(DIAG_OTA_ROLLBACK, DIAG_SEV_ERROR, running, 0, 0, reason);
        return esp_ota_mark_app_invalid_rollback_and_reboot();
    }

    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA pending verify accepted for partition=%s", partition_label_or_none(running));
        firmware_ota_log_event(DIAG_OTA_MARK_VALID, DIAG_SEV_INFO, running, 0, 0, 0);
    } else {
        ESP_LOGE(TAG, "OTA mark valid failed: %s", esp_err_to_name(ret));
        firmware_ota_log_event(DIAG_OTA_MARK_VALID, DIAG_SEV_ERROR, running, 0, (uint32_t)ret, 0);
    }
    return ret;
}

esp_err_t firmware_ota_begin(size_t image_size, const char *target_version)
{
    firmware_ota_blocker_t blocker = firmware_ota_get_blocker();
    if (blocker != FIRMWARE_OTA_BLOCKER_NONE) {
        ESP_LOGW(TAG, "OTA begin rejected: blocker=%s", firmware_ota_blocker_name(blocker));
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_WARN, NULL, 0, 0, (uint32_t)blocker);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_begin_rejected");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_ERROR, NULL, 0, 0, FIRMWARE_OTA_BLOCKER_NO_PARTITION);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_no_update_partition");
        return ESP_ERR_NOT_FOUND;
    }

    if (image_size > 0 && image_size != OTA_SIZE_UNKNOWN && image_size > partition->size) {
        ESP_LOGW(
            TAG,
            "OTA begin rejected: image_size=%u partition=%s size=%u",
            (unsigned)image_size,
            partition->label,
            (unsigned)partition->size);
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_ERROR, partition, (uint32_t)image_size, ESP_ERR_INVALID_SIZE, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_image_too_large");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t ret = esp_ota_begin(partition, image_size > 0 ? image_size : OTA_SIZE_UNKNOWN, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed partition=%s: %s", partition->label, esp_err_to_name(ret));
        firmware_ota_log_version_values(DIAG_OTA_BEGIN, partition, listener_device_get_fw_version(), target_version);
        firmware_ota_log_event(DIAG_OTA_BEGIN, DIAG_SEV_ERROR, partition, (uint32_t)image_size, (uint32_t)ret, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_begin_failed");
        return ret;
    }

    firmware_ota_resume_clear_journal();
    firmware_ota_lock();
    s_ota.handle = handle;
    s_ota.update_partition = partition;
    s_ota.expected_size = image_size;
    s_ota.bytes_written = 0;
    s_ota.active = true;
    snprintf(s_ota.target_version, sizeof(s_ota.target_version), "%s", target_version != NULL ? target_version : "unknown");
    firmware_ota_unlock();

    ESP_LOGI(
        TAG,
        "OTA begin partition=%s offset=0x%08" PRIx32 " partition_size=%u image_size=%u from=%s to=%s",
        partition->label,
        partition_offset_u32(partition),
        (unsigned)partition->size,
        (unsigned)image_size,
        listener_device_get_fw_version(),
        target_version != NULL ? target_version : "unknown");
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_UPDATE, partition);
    firmware_ota_log_event(DIAG_OTA_BEGIN, DIAG_SEV_INFO, partition, (uint32_t)image_size, 0, 0);
    firmware_ota_log_version_event(DIAG_OTA_BEGIN, partition);
    firmware_ota_set_runtime_active(true, 0, image_size, "ota_begin");
    return ESP_OK;
}

esp_err_t firmware_ota_begin_or_resume(
    size_t image_size,
    const char *target_version,
    const uint8_t sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES],
    bool *out_resumed)
{
    if (sha256 == NULL || image_size == 0 || image_size > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_resumed != NULL) {
        *out_resumed = false;
    }

    firmware_ota_lock();
    bool active_match = s_ota.active &&
                        s_ota.resume_identity_valid &&
                        s_ota.expected_size == image_size &&
                        memcmp(s_ota.resume_sha256, sha256, FIRMWARE_OTA_RESUME_SHA256_BYTES) == 0;
    bool active = s_ota.active;
    size_t active_bytes = s_ota.bytes_written;
    const esp_partition_t *active_partition = s_ota.update_partition;
    firmware_ota_unlock();

    if (active_match) {
        if (out_resumed != NULL) {
            *out_resumed = true;
        }
        ESP_LOGI(
            TAG,
            "OTA resume reused active session partition=%s bytes=%u/%u",
            partition_label_or_none(active_partition),
            (unsigned)active_bytes,
            (unsigned)image_size);
        firmware_ota_set_runtime_active(true, active_bytes, image_size, "ota_resume_active");
        return ESP_OK;
    }

    if (active) {
        ESP_LOGW(TAG, "OTA resume identity changed; discarding the previous partial image");
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
    }

    firmware_ota_blocker_t blocker = firmware_ota_get_blocker();
    if (blocker != FIRMWARE_OTA_BLOCKER_NONE) {
        ESP_LOGW(TAG, "OTA resume rejected: blocker=%s", firmware_ota_blocker_name(blocker));
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_WARN, NULL, 0, 0, (uint32_t)blocker);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_resume_rejected");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    firmware_ota_resume_record_t record = {0};
    esp_err_t journal_ret = firmware_ota_resume_load_journal(&record);
    if (journal_ret == ESP_OK &&
        firmware_ota_resume_record_matches(&record, partition, image_size, sha256)) {
        esp_ota_handle_t handle = 0;
        esp_err_t ret = esp_ota_resume(
            partition,
            OTA_WITH_SEQUENTIAL_WRITES,
            record.bytes_written,
            &handle);
        if (ret == ESP_OK) {
            firmware_ota_lock();
            s_ota.handle = handle;
            s_ota.update_partition = partition;
            s_ota.expected_size = image_size;
            s_ota.bytes_written = record.bytes_written;
            s_ota.active = true;
            s_ota.resume_identity_valid = true;
            s_ota.resume_checkpoint_bytes = record.bytes_written;
            memcpy(s_ota.resume_sha256, sha256, sizeof(s_ota.resume_sha256));
            snprintf(s_ota.target_version, sizeof(s_ota.target_version), "%s", target_version != NULL ? target_version : "unknown");
            firmware_ota_unlock();

            if (out_resumed != NULL) {
                *out_resumed = true;
            }
            ESP_LOGI(
                TAG,
                "OTA resume restored partition=%s bytes=%u/%u",
                partition_label_or_none(partition),
                (unsigned)record.bytes_written,
                (unsigned)image_size);
            firmware_ota_log_partition_event(DIAG_OTA_PARTITION_UPDATE, partition);
            firmware_ota_log_event(DIAG_OTA_BEGIN, DIAG_SEV_INFO, partition, record.bytes_written, 0, 1);
            firmware_ota_set_runtime_active(true, record.bytes_written, image_size, "ota_resume");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "OTA resume failed partition=%s: %s; starting fresh", partition_label_or_none(partition), esp_err_to_name(ret));
    }

    firmware_ota_resume_clear_journal();
    esp_err_t ret = firmware_ota_begin(image_size, target_version);
    if (ret != ESP_OK) {
        return ret;
    }

    firmware_ota_lock();
    const esp_partition_t *started_partition = s_ota.update_partition;
    s_ota.resume_identity_valid = true;
    s_ota.resume_checkpoint_bytes = 0;
    memcpy(s_ota.resume_sha256, sha256, sizeof(s_ota.resume_sha256));
    firmware_ota_unlock();

    ret = firmware_ota_resume_store_journal(started_partition, image_size, 0, sha256);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA resume journal initial checkpoint failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

esp_err_t firmware_ota_write(const void *data, size_t size)
{
    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    firmware_ota_lock();
    bool active = s_ota.active;
    esp_ota_handle_t handle = s_ota.handle;
    const esp_partition_t *partition = s_ota.update_partition;
    size_t next_size = s_ota.bytes_written + size;
    size_t expected_size = s_ota.expected_size;
    firmware_ota_unlock();

    if (!active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (expected_size > 0 && next_size > expected_size) {
        firmware_ota_log_event(DIAG_OTA_WRITE, DIAG_SEV_ERROR, partition, (uint32_t)next_size, ESP_ERR_INVALID_SIZE, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_write_size_rejected");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_ota_write(handle, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed at offset=%u: %s", (unsigned)(next_size - size), esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_WRITE, partition);
        firmware_ota_log_event(DIAG_OTA_WRITE, DIAG_SEV_ERROR, partition, (uint32_t)(next_size - size), (uint32_t)ret, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_write_failed");
        return ret;
    }

    uint8_t resume_sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES] = {0};
    bool checkpoint_due = false;
    firmware_ota_lock();
    s_ota.bytes_written = next_size;
    if (s_ota.resume_identity_valid &&
        next_size - s_ota.resume_checkpoint_bytes >= FIRMWARE_OTA_RESUME_CHECKPOINT_BYTES) {
        memcpy(resume_sha256, s_ota.resume_sha256, sizeof(resume_sha256));
        checkpoint_due = true;
    }
    firmware_ota_unlock();

    if (checkpoint_due) {
        ret = firmware_ota_resume_store_journal(partition, expected_size, next_size, resume_sha256);
        if (ret == ESP_OK) {
            firmware_ota_lock();
            if (s_ota.active && s_ota.bytes_written >= next_size) {
                s_ota.resume_checkpoint_bytes = next_size;
            }
            firmware_ota_unlock();
        } else {
            ESP_LOGW(TAG, "OTA resume checkpoint failed at bytes=%u: %s", (unsigned)next_size, esp_err_to_name(ret));
        }
    }
    firmware_ota_set_runtime_active(true, next_size, expected_size, NULL);
    return ESP_OK;
}

esp_err_t firmware_ota_finish(bool reboot_after_set_boot)
{
    firmware_ota_lock();
    bool active = s_ota.active;
    esp_ota_handle_t handle = s_ota.handle;
    const esp_partition_t *partition = s_ota.update_partition;
    size_t bytes_written = s_ota.bytes_written;
    size_t expected_size = s_ota.expected_size;
    firmware_ota_unlock();

    if (!active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (expected_size > 0 && bytes_written != expected_size) {
        ESP_LOGE(TAG, "OTA finish rejected: bytes=%u expected=%u", (unsigned)bytes_written, (unsigned)expected_size);
        firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);
        firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, ESP_ERR_INVALID_SIZE, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_finish_size_mismatch");
        firmware_ota_abort(ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_ota_end(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA image verify failed: %s", esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);
        firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, (uint32_t)ret, 0);
        firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_verify_failed");
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_verify_failed");
        firmware_ota_resume_clear_journal();
        firmware_ota_lock();
        firmware_ota_reset_session_locked();
        firmware_ota_unlock();
        return ret;
    }
    firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_INFO, partition, (uint32_t)bytes_written, 0, 0);
    firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);

    ret = esp_ota_set_boot_partition(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA set boot partition failed: %s", esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_SET_BOOT, partition);
        firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, (uint32_t)ret, 0);
        firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_set_boot_failed");
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_set_boot_failed");
        firmware_ota_resume_clear_journal();
        firmware_ota_lock();
        firmware_ota_reset_session_locked();
        firmware_ota_unlock();
        return ret;
    }

    ESP_LOGI(TAG, "OTA set boot partition=%s offset=0x%08" PRIx32 " bytes=%u; reboot=%u",
             partition->label,
             partition_offset_u32(partition),
             (unsigned)bytes_written,
             reboot_after_set_boot ? 1U : 0U);
    firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_INFO, partition, (uint32_t)bytes_written, 0, 0);
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_BOOT, partition);
    firmware_ota_log_version_event(DIAG_OTA_SET_BOOT, partition);

    firmware_ota_resume_clear_journal();
    firmware_ota_lock();
    s_ota.boot_partition = esp_ota_get_boot_partition();
    firmware_ota_reset_session_locked();
    firmware_ota_unlock();
    firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_finish");
    status_led_notify_success("ota_finish");

    if (reboot_after_set_boot) {
        firmware_ota_reboot_to_pending_image();
    }
    return ESP_OK;
}

void firmware_ota_abort(uint32_t reason)
{
    firmware_ota_lock();
    bool active = s_ota.active;
    esp_ota_handle_t handle = s_ota.handle;
    const esp_partition_t *partition = s_ota.update_partition;
    size_t bytes_written = s_ota.bytes_written;
    char target_version[FIRMWARE_OTA_VERSION_BYTES] = {0};
    snprintf(target_version, sizeof(target_version), "%s", s_ota.target_version[0] != '\0' ? s_ota.target_version : "unknown");
    firmware_ota_reset_session_locked();
    firmware_ota_unlock();
    firmware_ota_set_runtime_active(false, bytes_written, 0, "ota_abort");

    if (active) {
        firmware_ota_resume_clear_journal();
        esp_err_t ret = esp_ota_abort(handle);
        ESP_LOGW(TAG, "OTA aborted partition=%s bytes=%u reason=%" PRIu32 " abort_ret=%s",
                 partition_label_or_none(partition), (unsigned)bytes_written, reason, esp_err_to_name(ret));
        firmware_ota_log_version_values(DIAG_OTA_ABORT, partition, listener_device_get_fw_version(), target_version);
        firmware_ota_log_event(DIAG_OTA_ABORT, ret == ESP_OK ? DIAG_SEV_WARN : DIAG_SEV_ERROR,
                               partition, (uint32_t)bytes_written, (uint32_t)ret, reason);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_abort");
    }
}

bool firmware_ota_suspend_for_resume(uint32_t reason)
{
    firmware_ota_lock();
    bool active = s_ota.active;
    bool resumable = s_ota.resume_identity_valid;
    esp_ota_handle_t handle = s_ota.handle;
    const esp_partition_t *partition = s_ota.update_partition;
    size_t bytes_written = s_ota.bytes_written;
    size_t expected_size = s_ota.expected_size;
    uint8_t sha256[FIRMWARE_OTA_RESUME_SHA256_BYTES] = {0};
    if (resumable) {
        memcpy(sha256, s_ota.resume_sha256, sizeof(sha256));
    }
    firmware_ota_unlock();

    if (!active || !resumable) {
        return false;
    }

    esp_err_t ret = firmware_ota_resume_store_journal(
        partition,
        expected_size,
        bytes_written,
        sha256);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA suspend could not persist bytes=%u: %s", (unsigned)bytes_written, esp_err_to_name(ret));
        return false;
    }

    ret = esp_ota_abort(handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA suspend could not release handle: %s", esp_err_to_name(ret));
        return false;
    }

    firmware_ota_lock();
    if (s_ota.active && s_ota.handle == handle) {
        firmware_ota_reset_session_locked();
    }
    firmware_ota_unlock();
    firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_suspend");
    firmware_ota_log_event(DIAG_OTA_ABORT, DIAG_SEV_INFO, partition, (uint32_t)bytes_written, 0, reason);
    ESP_LOGI(
        TAG,
        "OTA suspended for resume partition=%s bytes=%u/%u reason=%" PRIu32,
        partition_label_or_none(partition),
        (unsigned)bytes_written,
        (unsigned)expected_size,
        reason);
    return true;
}

void firmware_ota_reboot_to_pending_image(void)
{
    ESP_LOGW(TAG, "OTA rebooting to pending image");
    firmware_ota_log_event(DIAG_OTA_REBOOT, DIAG_SEV_INFO, esp_ota_get_boot_partition(), 0, 0, 0);
    esp_restart();
}

static esp_err_t firmware_ota_boot_inactive_for_test(void)
{
    firmware_ota_blocker_t blocker = firmware_ota_get_blocker();
    if (blocker != FIRMWARE_OTA_BLOCKER_NONE) {
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_WARN, NULL, 0, 0, (uint32_t)blocker);
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_ERROR, NULL, 0, 0, FIRMWARE_OTA_BLOCKER_NO_PARTITION);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = esp_ota_set_boot_partition(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA test boot inactive failed partition=%s: %s", partition->label, esp_err_to_name(ret));
        firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_ERROR, partition, 0, (uint32_t)ret, DIAG_OTA_TEST_BOOT_INACTIVE);
        return ret;
    }

    ESP_LOGW(TAG, "OTA test boot inactive partition=%s; rebooting", partition->label);
    firmware_ota_log_version_event(DIAG_OTA_SET_BOOT, partition);
    firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_INFO, partition, 0, 0, DIAG_OTA_TEST_BOOT_INACTIVE);
    firmware_ota_reboot_to_pending_image();
    return ESP_OK;
}

firmware_ota_status_t firmware_ota_get_status(void)
{
    firmware_ota_status_t status = {0};
    firmware_ota_lock();
    const esp_partition_t *running = s_ota.running_partition != NULL ? s_ota.running_partition : esp_ota_get_running_partition();
    const esp_partition_t *boot = s_ota.boot_partition != NULL ? s_ota.boot_partition : esp_ota_get_boot_partition();
    const esp_partition_t *next = s_ota.active ? s_ota.update_partition : esp_ota_get_next_update_partition(NULL);
    status.running_partition = partition_label_or_none(running);
    status.boot_partition = partition_label_or_none(boot);
    status.update_partition = partition_label_or_none(next);
    status.running_offset = partition_offset_u32(running);
    status.boot_offset = partition_offset_u32(boot);
    status.update_offset = partition_offset_u32(next);
    status.update_size = partition_size_u32(next);
    status.running_version = listener_device_get_fw_version();
    status.target_version = s_ota.target_version[0] != '\0' ? s_ota.target_version : "none";
    status.bytes_written = s_ota.bytes_written;
    status.expected_size = s_ota.expected_size;
    status.active = s_ota.active;
    status.pending_verify = firmware_ota_running_pending_verify();
    status.running_state = ota_state_for_partition(running);
    firmware_ota_unlock();
    status.ready_mask = listener_device_get_ready_mask();
    status.degraded_mask = listener_device_get_degraded_mask();
    status.readiness = listener_device_get_factory_readiness();
    status.capabilities = listener_device_get_capabilities();
    status.blocker = firmware_ota_get_blocker();
    return status;
}

void firmware_ota_note_battery(uint8_t percent, uint32_t voltage_mv, bool valid)
{
    firmware_ota_lock();
    s_ota.battery_percent = percent;
    s_ota.battery_mv = voltage_mv;
    s_ota.battery_valid = valid;
    firmware_ota_unlock();
}

static void firmware_ota_print_status(void)
{
    firmware_ota_status_t status = firmware_ota_get_status();
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_RUNNING, esp_ota_get_running_partition());
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_BOOT, esp_ota_get_boot_partition());
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_NEXT, esp_ota_get_next_update_partition(NULL));
    ESP_LOGI(
        TAG,
        "OTA STATUS running=%s running_offset=0x%08" PRIx32 " boot=%s boot_offset=0x%08" PRIx32 " update=%s update_offset=0x%08" PRIx32 " update_size=%u version=%s target=%s active=%u pending_verify=%u state=%" PRIu32 " bytes=%u expected=%u blocker=%s ready_mask=0x%08" PRIx32 " degraded_mask=0x%08" PRIx32 " readiness=%s capabilities=%s",
        status.running_partition,
        status.running_offset,
        status.boot_partition,
        status.boot_offset,
        status.update_partition,
        status.update_offset,
        (unsigned)status.update_size,
        status.running_version,
        status.target_version,
        status.active ? 1U : 0U,
        status.pending_verify ? 1U : 0U,
        status.running_state,
        (unsigned)status.bytes_written,
        (unsigned)status.expected_size,
        firmware_ota_blocker_name(status.blocker),
        status.ready_mask,
        status.degraded_mask,
        status.readiness,
        status.capabilities);
}

bool firmware_ota_consume_usb_command(const char *line)
{
    if (line == NULL) {
        return false;
    }
    if (*line == '~') {
        line++;
    }

    const size_t prefix_len = strlen(FIRMWARE_OTA_USB_PREFIX);
    if (strncmp(line, FIRMWARE_OTA_USB_PREFIX, prefix_len) != 0) {
        return false;
    }

    const char *cmd = line + prefix_len;
    if (strcmp(cmd, "STATUS") == 0) {
        firmware_ota_print_status();
        return true;
    }
    if (strcmp(cmd, "BLOCKER") == 0) {
        firmware_ota_blocker_t blocker = firmware_ota_get_blocker();
        ESP_LOGI(TAG, "OTA BLOCKER: %s", firmware_ota_blocker_name(blocker));
        return true;
    }
    if (strcmp(cmd, "ABORT") == 0) {
        firmware_ota_abort(DIAG_OTA_ABORT_USB);
        return true;
    }
    if (strcmp(cmd, "TEST_BOOT_INACTIVE") == 0) {
        esp_err_t ret = firmware_ota_boot_inactive_for_test();
        ESP_LOGI(TAG, "OTA TEST_BOOT_INACTIVE command result=%s", esp_err_to_name(ret));
        return true;
    }

    ESP_LOGW(TAG, "OTA unknown command: %s", cmd);
    return true;
}
