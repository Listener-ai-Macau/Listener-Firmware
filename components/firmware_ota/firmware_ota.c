#include "firmware_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "audio_capture.h"
#include "ble_audio_stream.h"
#include "diag_log.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_flash_partitions.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "denzic_ota_orchestration_v1.h"
#include "listener_device.h"
#include "power_manager.h"
#include "status_led.h"

extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_schedule_active_connection(void) __attribute__((weak));
extern void ble_firmware_ota_on_firmware_abort(uint32_t reason) __attribute__((weak));

static const char *TAG = "firmware_ota";

#define FIRMWARE_OTA_USB_PREFIX "OTA:"
#define FIRMWARE_OTA_VERSION_BYTES 32
#define FIRMWARE_OTA_MIN_BATTERY_PERCENT 20
#define FIRMWARE_OTA_INACTIVITY_TIMEOUT_MS (3U * 60U * 1000U)
/* 干净运行兜底：好固件若始终未建立 BLE 链路（用户从未连接），到达此时间后用当前
 * 就绪信号确认一次，避免长期卡 pending-verify 而阻塞后续 OTA。事件驱动确认先到此
 * 则此回调为 no-op。崩溃式故障在到时前已由 ESP-IDF rollback 机制回滚。 */
#define FIRMWARE_OTA_PENDING_VERIFY_FALLBACK_MS (60U * 1000U)
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
    uint64_t pending_observability_correlation_id;
    int64_t inactivity_deadline_us;
    char target_version[FIRMWARE_OTA_VERSION_BYTES];
} firmware_ota_ctx_t;

static firmware_ota_ctx_t s_ota;
static esp_timer_handle_t s_inactivity_timer;
static esp_timer_handle_t s_pending_verify_fallback_timer;

static void firmware_ota_inactivity_timer_callback(void *arg);

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

static uint64_t firmware_ota_take_observability_correlation(void)
{
    firmware_ota_lock();
    uint64_t correlation_id = s_ota.pending_observability_correlation_id;
    s_ota.pending_observability_correlation_id = 0;
    firmware_ota_unlock();
    return correlation_id;
}

static void firmware_ota_log_observability_correlation(uint64_t correlation_id)
{
    if (correlation_id == 0) {
        return;
    }
    diag_log(
        DIAG_SRC_OTA,
        DIAG_OTA_CORRELATION,
        DIAG_SEV_INFO,
        (uint32_t)(correlation_id >> 32),
        (uint32_t)correlation_id,
        0,
        0);
}

static void firmware_ota_reset_session_locked(void)
{
    s_ota.handle = 0;
    s_ota.update_partition = NULL;
    s_ota.expected_size = 0;
    s_ota.bytes_written = 0;
    s_ota.active = false;
    s_ota.inactivity_deadline_us = 0;
    s_ota.target_version[0] = '\0';
}

static void firmware_ota_request_active_ble_connection(const char *reason)
{
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (ble_hid_gap_schedule_active_connection != NULL) {
        ret = ble_hid_gap_schedule_active_connection();
    } else if (ble_hid_gap_request_active_connection != NULL) {
        ret = ble_hid_gap_request_active_connection();
    } else {
        return;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA active BLE connection request scheduling failed reason=%s ret=%s",
                 reason != NULL ? reason : "ota", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "OTA scheduled active BLE connection parameters reason=%s",
                 reason != NULL ? reason : "ota");
    }
}

static void firmware_ota_stop_inactivity_timer(void)
{
    if (s_inactivity_timer != NULL) {
        (void)esp_timer_stop(s_inactivity_timer);
    }
}

static void firmware_ota_refresh_inactivity_timeout(void)
{
    if (s_inactivity_timer == NULL) {
        return;
    }

    int64_t deadline_us = denzic_ota_orchestration_v1_inactivity_deadline_us(
        esp_timer_get_time(), FIRMWARE_OTA_INACTIVITY_TIMEOUT_MS);
    firmware_ota_lock();
    if (s_ota.active) {
        s_ota.inactivity_deadline_us = deadline_us;
    }
    firmware_ota_unlock();

    (void)esp_timer_stop(s_inactivity_timer);
    esp_err_t ret = esp_timer_start_once(
        s_inactivity_timer,
        (uint64_t)FIRMWARE_OTA_INACTIVITY_TIMEOUT_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA inactivity timer start failed: %s", esp_err_to_name(ret));
    }
}

static void firmware_ota_inactivity_timer_callback(void *arg)
{
    (void)arg;
    int64_t deadline_us = 0;
    bool active = false;
    firmware_ota_lock();
    active = s_ota.active;
    if (active) {
        deadline_us = s_ota.inactivity_deadline_us;
    }
    firmware_ota_unlock();

    if (!active) {
        return;
    }

    /* 到期判断走平台层纯函数；未到期则按剩余时间重新调度，到期则 abort 陈旧会话。 */
    int64_t now_us = esp_timer_get_time();
    if (!denzic_ota_orchestration_v1_inactivity_expired(deadline_us, now_us)) {
        int64_t remaining_us = deadline_us - now_us;
        if (remaining_us < 0) {
            remaining_us = 0;
        }
        (void)esp_timer_start_once(s_inactivity_timer, (uint64_t)remaining_us);
        return;
    }

    ESP_LOGW(TAG,
             "OTA inactivity timeout after %u ms; aborting stale session and clearing OTA feedback",
             (unsigned)FIRMWARE_OTA_INACTIVITY_TIMEOUT_MS);
    firmware_ota_abort(DIAG_OTA_ABORT_IDLE_TIMEOUT);
}

static void firmware_ota_pending_verify_fallback_callback(void *arg)
{
    (void)arg;
    /* 仅在仍处于 pending-verify 时确认；事件驱动路径先到则此处为 no-op。 */
    if (!firmware_ota_running_pending_verify()) {
        return;
    }
    ESP_LOGI(TAG,
             "OTA pending-verify fallback: confirming after %us of clean uptime using current readiness signals",
             (unsigned)(FIRMWARE_OTA_PENDING_VERIFY_FALLBACK_MS / 1000U));
    (void)firmware_ota_confirm_pending_verify_if_ready();
}

static void firmware_ota_set_runtime_active(bool active, size_t bytes_written, size_t expected_size, const char *reason)
{
    power_manager_set_blocker(POWER_MANAGER_BLOCKER_OTA, active);
    if (!active) {
        firmware_ota_stop_inactivity_timer();
    }
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

static firmware_ota_blocker_t firmware_ota_blocker_from_platform(
    denzic_ota_orchestration_v1_blocker_t platform)
{
    switch (platform) {
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_IN_PROGRESS:      return FIRMWARE_OTA_BLOCKER_IN_PROGRESS;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_PENDING_VERIFY:   return FIRMWARE_OTA_BLOCKER_PENDING_VERIFY;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_RECORDING_ACTIVE: return FIRMWARE_OTA_BLOCKER_RECORDING_ACTIVE;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_BLE_AUDIO_ACTIVE: return FIRMWARE_OTA_BLOCKER_BLE_AUDIO_ACTIVE;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_DIAG_EXPORT_ACTIVE: return FIRMWARE_OTA_BLOCKER_DIAG_EXPORT_ACTIVE;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_LOW_BATTERY:      return FIRMWARE_OTA_BLOCKER_LOW_BATTERY;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_NO_PARTITION:     return FIRMWARE_OTA_BLOCKER_NO_PARTITION;
    case DENZIC_OTA_ORCHESTRATION_V1_BLOCKER_NONE:
    default:                                                    return FIRMWARE_OTA_BLOCKER_NONE;
    }
}

firmware_ota_blocker_t firmware_ota_get_blocker(void)
{
    /* 决策逻辑走平台层纯函数 denzic_ota_orchestration_v1_evaluate_blocker，
     * 产品层只负责采样设备事实（活动标志、电量、分区表）并执行返回的决策。 */
    denzic_ota_orchestration_v1_blocker_inputs_t inputs = {
        .session_active = false,
        .running_pending_verify = false,
        .recording_active = false,
        .ble_audio_active = false,
        .diag_export_active = false,
        .battery_valid = false,
        .battery_percent = 0,
        .has_update_partition = false,
    };

    firmware_ota_lock();
    inputs.session_active = s_ota.active;
    inputs.running_pending_verify = firmware_ota_running_pending_verify();
    inputs.recording_active = audio_capture_session_is_active();
    inputs.ble_audio_active = ble_audio_stream_is_busy();
    inputs.diag_export_active = diag_log_is_dumping();
    inputs.battery_valid = s_ota.battery_valid;
    inputs.battery_percent = s_ota.battery_percent;
    inputs.has_update_partition = (esp_ota_get_next_update_partition(NULL) != NULL);
    firmware_ota_unlock();

    denzic_ota_orchestration_v1_blocker_t platform = denzic_ota_orchestration_v1_evaluate_blocker(
        &inputs, FIRMWARE_OTA_MIN_BATTERY_PERCENT);
    return firmware_ota_blocker_from_platform(platform);
}

esp_err_t firmware_ota_init(void)
{
    if (s_ota.mutex == NULL) {
        s_ota.mutex = xSemaphoreCreateMutex();
        /* 修复1：mutex 创建失败（堆耗尽）属致命——后续 lock/unlock 退化为 no-op，OTA 数据路径
         * 将无同步运行。返回错误让 main.c 记录，且 register_gatt 内 firmware_ota_is_ready()
         * 守卫会拒绝注册 OTA GATT，从根上避免无锁裸奔。 */
        if (s_ota.mutex == NULL) {
            ESP_LOGE(TAG, "OTA mutex create failed; OTA disabled");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_inactivity_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = firmware_ota_inactivity_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ota_idle",
            .skip_unhandled_events = true,
        };
        esp_err_t timer_ret = esp_timer_create(&timer_args, &s_inactivity_timer);
        if (timer_ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA inactivity timer create failed: %s", esp_err_to_name(timer_ret));
        }
    }
    if (s_pending_verify_fallback_timer == NULL) {
        const esp_timer_create_args_t fallback_args = {
            .callback = firmware_ota_pending_verify_fallback_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ota_pv_fb",
            .skip_unhandled_events = true,
        };
        esp_err_t fb_create_ret = esp_timer_create(&fallback_args, &s_pending_verify_fallback_timer);
        if (fb_create_ret != ESP_OK) {
            ESP_LOGW(TAG, "OTA pending-verify fallback timer create failed: %s", esp_err_to_name(fb_create_ret));
        }
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

    /* pending-verify 镜像：启动一次兜底确认计时器。事件驱动（真实 BLE 安全连接）通常
     * 先到并完成确认；若始终未连接，到时后用当前就绪信号确认一次，避免长期卡 pending。 */
    if (state == ESP_OTA_IMG_PENDING_VERIFY && s_pending_verify_fallback_timer != NULL) {
        esp_err_t fb_ret = esp_timer_start_once(
            s_pending_verify_fallback_timer,
            (uint64_t)FIRMWARE_OTA_PENDING_VERIFY_FALLBACK_MS * 1000ULL);
        if (fb_ret != ESP_OK) {
            ESP_LOGW(TAG, "OTA pending-verify fallback timer start failed: %s", esp_err_to_name(fb_ret));
        }
    }
    return ESP_OK;
}

bool firmware_ota_is_ready(void)
{
    /* 修复1：register_gatt 在注册 OTA GATT 前检查——init 失败（mutex 未建）则不注册，
     * OTA 不可达，从根上避免无锁裸奔。 */
    return s_ota.mutex != NULL;
}

void firmware_ota_record_self_check(bool post_ok, bool ble_ready, bool keyboard_ready)
{
    firmware_ota_lock();
    s_ota.post_ok = post_ok;
    s_ota.ble_ready = ble_ready;
    s_ota.keyboard_ready = keyboard_ready;
    firmware_ota_unlock();
}

void firmware_ota_set_observability_correlation(uint64_t correlation_id)
{
    if (correlation_id == 0) {
        return;
    }
    firmware_ota_lock();
    if (!s_ota.active) {
        s_ota.pending_observability_correlation_id = correlation_id;
    }
    firmware_ota_unlock();
}

esp_err_t firmware_ota_confirm_pending_verify_if_ready(void)
{
    /* 决策走平台层纯函数 denzic_ota_orchestration_v1_decide_pending_verify；产品层只负责
     * 采样就绪信号并执行 mark-valid / mark-invalid-rollback。平台 rollback_reason_mask 位
     * 定义（POST=0x01 / BLE=0x02 / KEYBOARD=0x04）与产品 DIAG_OTA_ROLLBACK_* 完全一致，
     * 直接透传给 diag_log。调用方负责保证只在确有证据时调用（POST 失败 / 真实链路 / 兜底）。 */
    if (!firmware_ota_running_pending_verify()) {
        return ESP_OK;
    }

    firmware_ota_lock();
    const bool post_ok = s_ota.post_ok;
    const bool ble_ready = s_ota.ble_ready;
    const bool keyboard_ready = s_ota.keyboard_ready;
    const esp_partition_t *running = s_ota.running_partition;
    firmware_ota_unlock();
    if (running == NULL) {
        running = esp_ota_get_running_partition();
    }

    denzic_ota_orchestration_v1_pending_decision_t decision =
        denzic_ota_orchestration_v1_decide_pending_verify(true, post_ok, ble_ready, keyboard_ready);

    if (decision.action == DENZIC_OTA_ORCHESTRATION_V1_PENDING_ROLLBACK) {
        ESP_LOGE(TAG,
                 "OTA pending verify self-check failed; rolling back reason=0x%08" PRIx32,
                 decision.rollback_reason_mask);
        firmware_ota_log_version_event(DIAG_OTA_ROLLBACK, running);
        firmware_ota_log_event(DIAG_OTA_ROLLBACK, DIAG_SEV_ERROR, running, 0, 0, decision.rollback_reason_mask);
        return esp_ota_mark_app_invalid_rollback_and_reboot();
    }

    if (decision.action == DENZIC_OTA_ORCHESTRATION_V1_PENDING_NONE) {
        return ESP_OK;
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

void firmware_ota_note_runtime_ble_readiness_and_confirm(void)
{
    /* 真实安全链路已建立（配对 + 加密成功），是比 BLE init 返回值更深的"链路可用"证据。
     * 升级 ble_ready 后跑平台决策；confirm 内部幂等（分区不再 pending 后为 no-op）。 */
    firmware_ota_lock();
    s_ota.ble_ready = true;
    firmware_ota_unlock();
    (void)firmware_ota_confirm_pending_verify_if_ready();
}

esp_err_t firmware_ota_begin(size_t image_size, const char *target_version)
{
    uint64_t correlation_id = firmware_ota_take_observability_correlation();
    firmware_ota_log_observability_correlation(correlation_id);
    firmware_ota_blocker_t blocker = firmware_ota_get_blocker();
    if (blocker != FIRMWARE_OTA_BLOCKER_NONE) {
        ESP_LOGW(
            TAG,
            "OTA begin REJECTED by device policy blocker=%s image_size=%u running=%s",
            firmware_ota_blocker_name(blocker),
            (unsigned)image_size,
            listener_device_get_fw_version());
        firmware_ota_log_event(DIAG_OTA_REJECTED, DIAG_SEV_WARN, NULL, 0, 0, (uint32_t)blocker);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_begin_rejected");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        ESP_LOGE(TAG, "OTA begin REJECTED: no update partition image_size=%u", (unsigned)image_size);
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
    /* 修复2：不预先擦除整个 image 分区（数 MB，会阻塞 NimBLE host task 数秒）。改用
     * SEQUENTIAL_WRITES：begin 仅初始化 handle，擦除分摊到每次 write（已在 worker task）。
     * 固件 OTA 严格顺序传输（denzic core 仅在 offset 匹配时才 driver.write），满足该模式
     * 顺序写约束。image_size 分区容量预检仍保留（上方），expected_size 仍用于 finish 校验。 */
    esp_err_t ret = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed partition=%s: %s", partition->label, esp_err_to_name(ret));
        firmware_ota_log_version_values(DIAG_OTA_BEGIN, partition, listener_device_get_fw_version(), target_version);
        firmware_ota_log_event(DIAG_OTA_BEGIN, DIAG_SEV_ERROR, partition, (uint32_t)image_size, (uint32_t)ret, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_begin_failed");
        return ret;
    }

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
    firmware_ota_refresh_inactivity_timeout();
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

    if (!active) {
        firmware_ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (expected_size > 0 && next_size > expected_size) {
        firmware_ota_unlock();
        firmware_ota_log_event(DIAG_OTA_WRITE, DIAG_SEV_ERROR, partition, (uint32_t)next_size, ESP_ERR_INVALID_SIZE, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_write_size_rejected");
        return ESP_ERR_INVALID_SIZE;
    }

    /* 修复3/5：持锁跑 esp_ota_write，与 abort/finish 串行访问同一 esp_ota handle，
     * 消除 write(worker) vs abort(timer/host) 的 handle race（砖机风险）。esp_ota_write
     * 通常 <50ms；期间 abort 会等锁——等 write 完成再取消，正是期望语义（不并发损坏）。 */
    esp_err_t ret = esp_ota_write(handle, data, size);
    if (ret == ESP_OK) {
        s_ota.bytes_written = next_size;
    }
    firmware_ota_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed at offset=%u: %s", (unsigned)(next_size - size), esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_WRITE, partition);
        firmware_ota_log_event(DIAG_OTA_WRITE, DIAG_SEV_ERROR, partition, (uint32_t)(next_size - size), (uint32_t)ret, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_write_failed");
        return ret;
    }
    firmware_ota_set_runtime_active(true, next_size, expected_size, NULL);
    firmware_ota_refresh_inactivity_timeout();
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

    if (!active) {
        firmware_ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (expected_size > 0 && bytes_written != expected_size) {
        /* size mismatch：内联 abort（已持锁，不能调 firmware_ota_abort——它会重复取锁死锁）。
         * 覆盖 abort 的关键副作用：esp_ota_abort + reset + ble 回调（触发 BLE 层 drain 队列）。 */
        esp_ota_abort(handle);
        firmware_ota_reset_session_locked();
        firmware_ota_unlock();
        ESP_LOGE(TAG, "OTA finish rejected: bytes=%u expected=%u", (unsigned)bytes_written, (unsigned)expected_size);
        firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);
        firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, ESP_ERR_INVALID_SIZE, 0);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_finish_size_mismatch");
        firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_finish_size_mismatch");
        firmware_ota_stop_inactivity_timer();
        if (ble_firmware_ota_on_firmware_abort != NULL) {
            ble_firmware_ota_on_firmware_abort(ESP_ERR_INVALID_SIZE);
        }
        return ESP_ERR_INVALID_SIZE;
    }

    /* 修复3/5：持锁跑 esp_ota_end + set_boot_partition，与 write/abort 串行访问同一 handle，
     * 消除 finish(host) vs write(worker)/abort(timer) 的 handle race（砖机风险）。 */
    esp_err_t ret = esp_ota_end(handle);
    if (ret != ESP_OK) {
        firmware_ota_reset_session_locked();
        firmware_ota_unlock();
        ESP_LOGE(TAG, "OTA image verify failed: %s", esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);
        firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, (uint32_t)ret, 0);
        firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_verify_failed");
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_verify_failed");
        return ret;
    }
    firmware_ota_log_event(DIAG_OTA_VERIFY, DIAG_SEV_INFO, partition, (uint32_t)bytes_written, 0, 0);
    firmware_ota_log_version_event(DIAG_OTA_VERIFY, partition);

    ret = esp_ota_set_boot_partition(partition);
    if (ret != ESP_OK) {
        firmware_ota_reset_session_locked();
        firmware_ota_unlock();
        ESP_LOGE(TAG, "OTA set boot partition failed: %s", esp_err_to_name(ret));
        firmware_ota_log_version_event(DIAG_OTA_SET_BOOT, partition);
        firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_ERROR, partition, (uint32_t)bytes_written, (uint32_t)ret, 0);
        firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_set_boot_failed");
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_HARD, "ota_set_boot_failed");
        return ret;
    }
    s_ota.boot_partition = esp_ota_get_boot_partition();
    firmware_ota_reset_session_locked();
    firmware_ota_unlock();

    ESP_LOGI(TAG, "OTA set boot partition=%s offset=0x%08" PRIx32 " bytes=%u; reboot=%u",
             partition->label,
             partition_offset_u32(partition),
             (unsigned)bytes_written,
             reboot_after_set_boot ? 1U : 0U);
    firmware_ota_log_event(DIAG_OTA_SET_BOOT, DIAG_SEV_INFO, partition, (uint32_t)bytes_written, 0, 0);
    firmware_ota_log_partition_event(DIAG_OTA_PARTITION_BOOT, partition);
    firmware_ota_log_version_event(DIAG_OTA_SET_BOOT, partition);
    firmware_ota_set_runtime_active(false, bytes_written, expected_size, "ota_finish");
    firmware_ota_stop_inactivity_timer();
    /* Owner 2026-07-28: OK peak + immediate reboot boot-feedback reads as two
     * power-on lights. When we reboot into the new image, boot LED alone is enough. */
    if (reboot_after_set_boot) {
        firmware_ota_reboot_to_pending_image();
    } else {
        status_led_notify_success("ota_finish");
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
    esp_err_t ret = ESP_OK;
    if (active) {
        /* 修复3/5：持锁跑 esp_ota_abort，与 write/finish 串行访问同一 esp_ota handle，
         * 消除 abort(timer/host) vs write(worker) 的 handle race（砖机风险）。先 abort handle 再清字段。 */
        ret = esp_ota_abort(handle);
    }
    firmware_ota_reset_session_locked();
    firmware_ota_unlock();
    firmware_ota_set_runtime_active(false, bytes_written, 0, "ota_abort");
    firmware_ota_stop_inactivity_timer();

    if (active) {
        ESP_LOGW(TAG, "OTA aborted partition=%s bytes=%u reason=%" PRIu32 " abort_ret=%s",
                 partition_label_or_none(partition), (unsigned)bytes_written, reason, esp_err_to_name(ret));
        firmware_ota_log_version_values(DIAG_OTA_ABORT, partition, listener_device_get_fw_version(), target_version);
        firmware_ota_log_event(DIAG_OTA_ABORT, ret == ESP_OK ? DIAG_SEV_WARN : DIAG_SEV_ERROR,
                               partition, (uint32_t)bytes_written, (uint32_t)ret, reason);
        status_led_set_error(STATUS_LED_ERROR_DOMAIN_OTA, STATUS_LED_ERROR_RETRYABLE, "ota_abort");
        if (ble_firmware_ota_on_firmware_abort != NULL) {
            ble_firmware_ota_on_firmware_abort(reason);
        }
    }
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
