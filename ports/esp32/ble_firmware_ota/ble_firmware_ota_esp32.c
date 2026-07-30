#include "ble_firmware_ota.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_capture.h"
#include "denzic_ota_v1.h"
#include "denzic_ota_v1_reorder.h"
#include "diag_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "firmware_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "listener_device.h"
#include "os/os_mbuf.h"
#include "watchdog_platform.h"

#define BLE_FIRMWARE_OTA_DATA_MAX_BYTES 512
#define BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX 500
/* Host negotiates window via BEGIN; default only applies when host sends 0.
 * Keep aligned with Type's preferred window (100) so status echoes a useful size. */
#define BLE_FIRMWARE_OTA_DEFAULT_WINDOW_CHUNKS 200
#define BLE_FIRMWARE_OTA_ACTIVE_LINK_RETRY_MS 100
#define BLE_FIRMWARE_OTA_REBOOT_DELAY_MS 500
#define BLE_FIRMWARE_OTA_REBOOT_DISCONNECT_TIMEOUT_MS 1000
#define BLE_FIRMWARE_OTA_REBOOT_DISCONNECT_POLL_MS 20
/* ESP-IDF Xtensa StackType_t is byte-addressed. Keep the proven 4 KB worker. */
#define BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES 4096
#define BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES 4096
/* Host pipelines ~24 WWR + window up to 128. Queue storage is static BSS so
 * boot heap fragmentation cannot skip OTA GATT registration (depth 64 via
 * xQueueCreate previously OOMed ~32 KB internal free and hid the service).
 * Depth 48 (~24 KB BSS) starved audio_capture task create (ESP_ERR_NO_MEM);
 * keep 32 (~16 KB) which matched the previously stable heap-backed size. */
#define BLE_FIRMWARE_OTA_WORKER_QUEUE_DEPTH 32
/* Platform dual-lane reorder (denzic_ota_v1_reorder); 16 slots × 512 B BSS. */
#define BLE_FIRMWARE_OTA_REORDER_SLOTS DENZIC_OTA_V1_REORDER_DEFAULT_SLOTS
#define BLE_FIRMWARE_OTA_DATA_ENQUEUE_TIMEOUT_MS 2000
#define BLE_FIRMWARE_OTA_WORKER_POLL_TIMEOUT_MS 10
#define BLE_FIRMWARE_OTA_WORKER_TASK_PRIORITY (tskIDLE_PRIORITY + 6)
#define BLE_FIRMWARE_OTA_AUDIO_SUSPEND_TIMEOUT_MS 250
#define BLE_FIRMWARE_OTA_STORAGE_BATCH_CHUNKS 8
#define BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES \
    (BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX * BLE_FIRMWARE_OTA_STORAGE_BATCH_CHUNKS)
#define BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES DENZIC_OTA_V1_PROTOCOL_NAME

typedef enum {
    BLE_FIRMWARE_OTA_GATT_ATTR_READINESS = 1,
    BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES = 2,
    BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL = 3,
    BLE_FIRMWARE_OTA_GATT_ATTR_DATA = 4,
    BLE_FIRMWARE_OTA_GATT_ATTR_STATUS = 5,
    BLE_FIRMWARE_OTA_GATT_ATTR_DATA_B = 6,
} ble_firmware_ota_gatt_attr_t;

/* 修复4 + 修复1：OTA 数据写入（含 flash）落到独立 worker 任务，不再阻塞 NimBLE host 任务。
 * s_ota 由 s_ota_lock 保护：worker 做 data/reset，NimBLE 做 control/status，二者互斥。
 * 来自任意任务（inactivity timer / USB / NimBLE / worker 自身处理 data 时）的 abort 不在
 * 回调里 memset s_ota，只置位 s_core_reset_pending；worker 下一次循环持锁执行 core_init，
 * 故 reset 绝不与并发的 data/control 写入交错——消除数据竞争（修复1）。 */
typedef struct {
    uint16_t length;
    uint8_t data[BLE_FIRMWARE_OTA_DATA_MAX_BYTES];
} ble_firmware_ota_data_job_t;

static const char *TAG = "ble_firmware_ota";
static const ble_uuid128_t s_service_uuid = BLE_FIRMWARE_OTA_SERVICE_UUID;
static const ble_uuid128_t s_readiness_uuid = BLE_FIRMWARE_OTA_READINESS_UUID;
static const ble_uuid128_t s_capabilities_uuid = BLE_FIRMWARE_OTA_CAPABILITIES_UUID;
static const ble_uuid128_t s_control_uuid = BLE_FIRMWARE_OTA_V1_CONTROL_UUID;
static const ble_uuid128_t s_data_uuid = BLE_FIRMWARE_OTA_V1_DATA_UUID;
static const ble_uuid128_t s_data_b_uuid = BLE_FIRMWARE_OTA_V1_DATA_B_UUID;
static const ble_uuid128_t s_status_uuid = BLE_FIRMWARE_OTA_V1_STATUS_UUID;
static denzic_ota_v1_context_t s_ota;
static TickType_t s_active_link_retry_tick;
static bool s_active_link_confirmed;
static bool s_registered;
static SemaphoreHandle_t s_ota_lock;
static QueueHandle_t s_ota_worker_queue;
static TaskHandle_t s_ota_worker_task;
/* Permanent worker touches flash; stack stays internal BSS (not xTaskCreate/PSRAM). */
static StaticTask_t s_ota_worker_tcb;
_Static_assert(sizeof(StackType_t) == 1, "ESP-IDF Xtensa task stacks must be byte addressed");
static StackType_t s_ota_worker_stack[BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES];
/* Queue item storage in BSS (not heap) so OTA service still registers under low heap. */
static uint8_t s_ota_worker_queue_storage[BLE_FIRMWARE_OTA_WORKER_QUEUE_DEPTH
                                         * sizeof(ble_firmware_ota_data_job_t)];
static StaticQueue_t s_ota_worker_queue_control;
/* Platform dual-lane reorder buffer (shared with Companion). */
static denzic_ota_v1_reorder_slot_t s_ota_reorder_slots[BLE_FIRMWARE_OTA_REORDER_SLOTS];
static denzic_ota_v1_reorder_t s_ota_reorder;
/* Allocate only for an active OTA, after boot-time audio DMA owns its memory.
 * Flash cache-disabled writes still require this payload to stay internal. */
static uint8_t *s_ota_storage_batch;
static size_t s_ota_storage_batch_length;
static uint32_t s_ota_storage_write_calls;
static uint64_t s_ota_storage_write_total_us;
static uint32_t s_ota_storage_write_max_us;
/* 由 on_firmware_abort 置位（任意任务，含 worker 持锁处理 data 时的回调路径）；worker 下次
 * 循环持锁清零并执行 core_init。volatile bool 单写多读在本平台原子，无需锁。 */
static volatile bool s_core_reset_pending;

extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_schedule_active_connection(void) __attribute__((weak));
extern bool ble_hid_gap_active_connection_applied(void) __attribute__((weak));
extern bool ble_hid_gap_ota_connection_ready(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void) __attribute__((weak));
extern bool ble_hid_gap_is_connected(void) __attribute__((weak));

static void ble_firmware_ota_lock(void)
{
    if (s_ota_lock != NULL) {
        xSemaphoreTake(s_ota_lock, portMAX_DELAY);
    }
}

static void ble_firmware_ota_unlock(void)
{
    if (s_ota_lock != NULL) {
        xSemaphoreGive(s_ota_lock);
    }
}

static int ble_firmware_ota_att_error_from_core(void)
{
    switch (s_ota.last_error) {
    case DENZIC_OTA_V1_ERROR_BAD_MAGIC:
    case DENZIC_OTA_V1_ERROR_BAD_SIZE:
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    case DENZIC_OTA_V1_ERROR_OFFSET_MISMATCH:
        return 0;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int ble_firmware_ota_copy_mbuf(
    struct os_mbuf *om,
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t *out_len)
{
    if (om == NULL || buffer == NULL || out_len == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(om);
    if ((size_t)len > buffer_size) {
        ESP_LOGW(TAG, "GATT write too large: len=%u max=%u", len, (unsigned)buffer_size);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (len > 0 && os_mbuf_copydata(om, 0, len, buffer) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    *out_len = len;
    return 0;
}

static void ble_firmware_ota_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DELAY_MS));

    esp_err_t disconnect_ret = ESP_ERR_NOT_SUPPORTED;
    if (ble_hid_gap_prepare_shutdown_disconnect != NULL) {
        disconnect_ret = ble_hid_gap_prepare_shutdown_disconnect();
    }
    const TickType_t disconnect_started = xTaskGetTickCount();
    if (disconnect_ret == ESP_OK && ble_hid_gap_is_connected != NULL) {
        const TickType_t disconnect_timeout =
            pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DISCONNECT_TIMEOUT_MS);
        while (ble_hid_gap_is_connected() &&
               (xTaskGetTickCount() - disconnect_started) < disconnect_timeout) {
            vTaskDelay(pdMS_TO_TICKS(BLE_FIRMWARE_OTA_REBOOT_DISCONNECT_POLL_MS));
        }
    }
    const bool still_connected =
        ble_hid_gap_is_connected != NULL && ble_hid_gap_is_connected();
    ESP_LOGI(
        TAG,
        "Denzic OTA v1 reboot boundary disconnect_ret=%s disconnected=%u elapsed_ms=%u",
        esp_err_to_name(disconnect_ret),
        still_connected ? 0U : 1U,
        (unsigned)((xTaskGetTickCount() - disconnect_started) * portTICK_PERIOD_MS));

    firmware_ota_reboot_to_pending_image();
    vTaskDelete(NULL);
}

static bool ble_firmware_ota_schedule_reboot(void)
{
    /* One-shot task: keep xTaskCreate (stack <= ALWAYSINTERNAL prefers internal).
     * Do not use a reusable static stack — vTaskDelete leaves static TCB unusable. */
    BaseType_t started = xTaskCreate(
        ble_firmware_ota_reboot_task,
        "ble_ota_reboot",
        BLE_FIRMWARE_OTA_REBOOT_TASK_STACK_BYTES,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL);
    return started == pdPASS;
}

static const char *ble_firmware_ota_error_name(uint8_t error)
{
    switch (error) {
    case DENZIC_OTA_V1_ERROR_NONE:
        return "none";
    case DENZIC_OTA_V1_ERROR_BAD_MAGIC:
        return "bad_magic";
    case DENZIC_OTA_V1_ERROR_BAD_STATE:
        return "bad_state";
    case DENZIC_OTA_V1_ERROR_BAD_SIZE:
        return "bad_size";
    case DENZIC_OTA_V1_ERROR_OFFSET_MISMATCH:
        return "offset_mismatch";
    case DENZIC_OTA_V1_ERROR_STORAGE_WRITE:
        return "storage_write";
    case DENZIC_OTA_V1_ERROR_STORAGE_BEGIN:
        return "storage_begin";
    case DENZIC_OTA_V1_ERROR_STORAGE_FINISH:
        return "storage_finish";
    default:
        return "unknown";
    }
}

static const char *ble_firmware_ota_op_name(uint8_t op)
{
    switch (op) {
    case DENZIC_OTA_V1_OP_BEGIN:
        return "BEGIN";
    case DENZIC_OTA_V1_OP_SYNC:
        return "SYNC";
    case DENZIC_OTA_V1_OP_FINISH:
        return "FINISH";
    case DENZIC_OTA_V1_OP_ABORT:
        return "ABORT";
    default:
        return "OTHER";
    }
}

static bool ble_firmware_ota_storage_begin(void *driver_context, uint32_t image_size)
{
    (void)driver_context;
    s_active_link_retry_tick = 0;
    s_active_link_confirmed = false;
    s_ota_storage_batch_length = 0;
    if (s_ota_storage_batch != NULL) {
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
    }
    s_ota_storage_write_calls = 0;
    s_ota_storage_write_total_us = 0;
    s_ota_storage_write_max_us = 0;
    esp_err_t audio_suspend_ret = audio_capture_set_ota_suspended(true);
    const TickType_t audio_suspend_started = xTaskGetTickCount();
    while (audio_suspend_ret == ESP_OK &&
           !audio_capture_idle_power_save_is_applied() &&
           (xTaskGetTickCount() - audio_suspend_started) <
               pdMS_TO_TICKS(BLE_FIRMWARE_OTA_AUDIO_SUSPEND_TIMEOUT_MS)) {
        vTaskDelay(1);
    }
    if (audio_suspend_ret != ESP_OK ||
        !audio_capture_idle_power_save_is_applied()) {
        (void)audio_capture_set_ota_suspended(false);
        ESP_LOGE(
            TAG,
            "Denzic OTA v1 audio suspend failed ret=%s applied=%u",
            esp_err_to_name(audio_suspend_ret),
            audio_capture_idle_power_save_is_applied());
        return false;
    }
    const size_t internal_free_before =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest_before =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_ota_storage_batch = heap_caps_malloc(
        BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_ota_storage_batch == NULL) {
        (void)audio_capture_set_ota_suspended(false);
        ESP_LOGE(
            TAG,
            "Denzic OTA v1 internal batch allocation failed bytes=%u free=%u largest=%u",
            (unsigned)BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES,
            (unsigned)internal_free_before,
            (unsigned)internal_largest_before);
        return false;
    }
    ESP_LOGI(
        TAG,
        "Denzic OTA v1 audio suspended and batch allocated bytes=%u internal_free=%u->%u largest=%u->%u",
        (unsigned)BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES,
        (unsigned)internal_free_before,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)internal_largest_before,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    /* New image session: drop any dual-lane futures from a prior attempt. */
    denzic_ota_v1_reorder_clear(&s_ota_reorder);
    const firmware_ota_blocker_t blocker_before = firmware_ota_get_blocker();
    esp_err_t ret = firmware_ota_begin(image_size, DENZIC_OTA_V1_PROTOCOL_NAME);
    if (ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Denzic OTA v1 storage begin accepted size=%u blocker_before=%s",
            (unsigned)image_size,
            firmware_ota_blocker_name(blocker_before));
    } else {
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
        (void)audio_capture_set_ota_suspended(false);
        /* Host UI often maps transport failure to "设备拒绝升级"; this line is the
         * device-side root cause when BEGIN reaches GATT (blocker / partition / erase). */
        ESP_LOGW(
            TAG,
            "Denzic OTA v1 storage begin REJECTED size=%u ret=%s blocker=%s",
            (unsigned)image_size,
            esp_err_to_name(ret),
            firmware_ota_blocker_name(firmware_ota_get_blocker()));
    }
    return ret == ESP_OK;
}

static bool ble_firmware_ota_storage_flush(void)
{
    if (s_ota_storage_batch_length == 0) {
        return true;
    }

    const size_t length = s_ota_storage_batch_length;
    s_ota_storage_batch_length = 0;
    const int64_t write_started_us = esp_timer_get_time();
    esp_err_t ret = firmware_ota_write(s_ota_storage_batch, length);
    uint32_t write_elapsed_us =
        (uint32_t)(esp_timer_get_time() - write_started_us);
    s_ota_storage_write_calls++;
    s_ota_storage_write_total_us += write_elapsed_us;
    if (write_elapsed_us > s_ota_storage_write_max_us) {
        s_ota_storage_write_max_us = write_elapsed_us;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Denzic OTA v1 batched data write failed len=%u ret=%s",
            (unsigned)length,
            esp_err_to_name(ret));
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return false;
    }
    return true;
}

static bool ble_firmware_ota_storage_write(
    void *driver_context,
    const uint8_t *data,
    size_t length)
{
    (void)driver_context;
    if (s_ota_storage_batch == NULL ||
        data == NULL ||
        length == 0 ||
        length > BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES) {
        return false;
    }
    if (s_ota_storage_batch_length + length > BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES &&
        !ble_firmware_ota_storage_flush()) {
        return false;
    }

    memcpy(s_ota_storage_batch + s_ota_storage_batch_length, data, length);
    s_ota_storage_batch_length += length;
    if (s_ota_storage_batch_length == BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES) {
        return ble_firmware_ota_storage_flush();
    }
    return true;
}

static bool ble_firmware_ota_storage_finish(void *driver_context)
{
    (void)driver_context;
    if (!ble_firmware_ota_storage_flush()) {
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
        return false;
    }
    esp_err_t ret = firmware_ota_finish(false);
    heap_caps_free(s_ota_storage_batch);
    s_ota_storage_batch = NULL;
    ESP_LOGI(
        TAG,
        "Denzic OTA v1 finish size=%u ret=%s flash_calls=%u flash_total_ms=%u flash_max_us=%u",
        (unsigned)s_ota.expected_size,
        esp_err_to_name(ret),
        (unsigned)s_ota_storage_write_calls,
        (unsigned)(s_ota_storage_write_total_us / 1000U),
        (unsigned)s_ota_storage_write_max_us);
    if (ret != ESP_OK) {
        (void)audio_capture_set_ota_suspended(false);
        return false;
    }
    if (!ble_firmware_ota_schedule_reboot()) {
        /* 修复5：firmware_ota_finish 已成功切好 boot 分区，此时唯一缺的是重启。原来调
         * firmware_ota_abort 是 no-op（finish 后 active=false），会导致 boot 分区已指向新
         * 镜像却不重启，停留在旧镜像直到下次手动重启。schedule_reboot 失败（xTaskCreate 失败，
         * 通常堆耗尽）属紧急路径——直接 esp_restart() 兜底；桌面端 finish 走 WriteWithResponse，
         * 其 reboot-handoff 容忍会吃掉这次 ATT 响应。 */
        ESP_LOGE(TAG, "Denzic OTA v1 reboot task scheduling failed; restarting now to boot pending image");
        esp_restart();
    }
    return true;
}

static void ble_firmware_ota_storage_abort(void *driver_context)
{
    (void)driver_context;
    s_ota_storage_batch_length = 0;
    heap_caps_free(s_ota_storage_batch);
    s_ota_storage_batch = NULL;
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
    (void)audio_capture_set_ota_suspended(false);
}

static void ble_firmware_ota_core_init(void)
{
    s_ota_storage_batch_length = 0;
    heap_caps_free(s_ota_storage_batch);
    s_ota_storage_batch = NULL;
    const denzic_ota_v1_storage_driver_t storage = {
        .begin = ble_firmware_ota_storage_begin,
        .write = ble_firmware_ota_storage_write,
        .finish = ble_firmware_ota_storage_finish,
        .abort = ble_firmware_ota_storage_abort,
    };
    denzic_ota_v1_init(
        &s_ota,
        storage,
        NULL,
        BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX,
        BLE_FIRMWARE_OTA_DEFAULT_WINDOW_CHUNKS);
    denzic_ota_v1_reorder_init(
        &s_ota_reorder,
        s_ota_reorder_slots,
        BLE_FIRMWARE_OTA_REORDER_SLOTS,
        BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX);
    s_active_link_retry_tick = 0;
    s_active_link_confirmed = false;
}

static bool ble_firmware_ota_tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void ble_firmware_ota_update_active_link(void)
{
    if (s_ota.state != DENZIC_OTA_V1_STATE_RECEIVING) {
        s_active_link_confirmed = false;
        denzic_ota_v1_set_status_flags(&s_ota, 0);
        return;
    }

    const bool active_link_applied = ble_hid_gap_active_connection_applied == NULL ||
                                     ble_hid_gap_active_connection_applied();
    const bool transfer_link_ready = ble_hid_gap_ota_connection_ready == NULL
        ? active_link_applied
        : ble_hid_gap_ota_connection_ready();
    if (transfer_link_ready) {
        if (!s_active_link_confirmed) {
            ESP_LOGI(TAG, "Denzic OTA v1 transfer-ready BLE link confirmed");
        }
        s_active_link_confirmed = true;
        denzic_ota_v1_set_status_flags(
            &s_ota,
            DENZIC_OTA_V1_STATUS_FLAG_ACTIVE_LINK_CONFIRMED);
    } else {
        s_active_link_confirmed = false;
        denzic_ota_v1_set_status_flags(&s_ota, 0);
    }

    if (active_link_applied) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_active_link_retry_tick != 0 &&
        !ble_firmware_ota_tick_reached(now, s_active_link_retry_tick)) {
        return;
    }

    s_active_link_retry_tick = now + pdMS_TO_TICKS(BLE_FIRMWARE_OTA_ACTIVE_LINK_RETRY_MS);
    if (ble_hid_gap_schedule_active_connection != NULL ||
        ble_hid_gap_request_active_connection != NULL) {
        esp_err_t ret = ble_hid_gap_schedule_active_connection != NULL
            ? ble_hid_gap_schedule_active_connection()
            : ble_hid_gap_request_active_connection();
        ESP_LOGI(TAG, "Denzic OTA v1 active BLE link pending ret=%s", esp_err_to_name(ret));
    }
}

static void ble_firmware_ota_sync_status(void)
{
    ble_firmware_ota_update_active_link();
    firmware_ota_status_t status = firmware_ota_get_status();
    if (s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING && status.active) {
        s_ota.bytes_written = (uint32_t)status.bytes_written;
        s_ota.expected_size = (uint32_t)status.expected_size;
    }
}

/* 修复4 + 修复1：OTA worker 任务。data 写入（含 flash）在此任务上执行，不阻塞 NimBLE host
 * 任务；reset（core_init）也在此任务上串行执行。100ms 轮询超时既能在 idle 时及时服务 abort
 * 请求，又能定期喂看门狗（若用 portMAX_DELAY 则无数据时永不喂狗→触发 WDT）。 */
static void ble_firmware_ota_worker_task(void *arg)
{
    (void)arg;
    (void)watchdog_platform_subscribe_current_task("ble_firmware_ota_worker");

    ble_firmware_ota_data_job_t job;
    while (1) {
        watchdog_platform_feed_current_task();
        bool have_job = xQueueReceive(
                            s_ota_worker_queue,
                            &job,
                            pdMS_TO_TICKS(BLE_FIRMWARE_OTA_WORKER_POLL_TIMEOUT_MS)) == pdTRUE;

        ble_firmware_ota_lock();
        if (s_core_reset_pending) {
            /* reset 优先于 data：串行 core_init，绝不与并发的 control/status 处理交错。 */
            s_core_reset_pending = false;
            /* 修复4：drain 队列里 abort 前残留的 stale data。否则若 host 紧接着发新 BEGIN，
             * 旧 session 的 data（offset≠0）会在新 session 的 RECEIVING 态下触发
             * OFFSET_MISMATCH，污染新一轮 OTA。drain 在 worker 持锁上下文，无并发。 */
            ble_firmware_ota_data_job_t stale;
            while (xQueueReceive(s_ota_worker_queue, &stale, 0) == pdTRUE) {
                /* 丢弃 stale data */
            }
            ble_firmware_ota_core_init();
            (void)audio_capture_set_ota_suspended(false);
        } else if (have_job) {
            /* Batch-drain while holding the lock so flash stays busy and host
             * SYNC/status sees a fresher offset (fewer recoveries / resends). */
            do {
                (void)denzic_ota_v1_handle_data_ordered(
                    &s_ota,
                    &s_ota_reorder,
                    job.data,
                    job.length);
                watchdog_platform_feed_current_task();
            } while (xQueueReceive(s_ota_worker_queue, &job, 0) == pdTRUE);
        }
        ble_firmware_ota_unlock();
    }
}

static void ble_firmware_ota_drain_queued_data_locked(void)
{
    /* NimBLE serializes callbacks, so SYNC/status can drain at most the fixed
     * 32-entry tail while holding the OTA lock. This preserves the 1.0.3
     * confirmed-offset fast path without making ordinary DATA writes run on
     * the callback task. */
    ble_firmware_ota_data_job_t job;
    while (xQueueReceive(s_ota_worker_queue, &job, 0) == pdTRUE) {
        (void)denzic_ota_v1_handle_data_ordered(
            &s_ota,
            &s_ota_reorder,
            job.data,
            job.length);
        watchdog_platform_feed_current_task();
    }
    denzic_ota_v1_reorder_drain(&s_ota, &s_ota_reorder);
    (void)ble_firmware_ota_storage_flush();
}

static int ble_firmware_ota_append_status(struct os_mbuf *om)
{
    uint8_t status[DENZIC_OTA_V1_STATUS_BYTES];
    ble_firmware_ota_lock();
    ble_firmware_ota_drain_queued_data_locked();
    ble_firmware_ota_sync_status();
    size_t length = denzic_ota_v1_encode_status(&s_ota, status, sizeof(status));
    ble_firmware_ota_unlock();
    if (length == 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(om, status, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int ble_firmware_ota_handle_control_write(struct os_mbuf *om)
{
    uint8_t control[DENZIC_OTA_V1_CONTROL_BYTES];
    uint16_t length = 0;
    int att_error = ble_firmware_ota_copy_mbuf(om, control, sizeof(control), &length);
    if (att_error != 0) {
        ESP_LOGW(TAG, "Denzic OTA v1 control copy failed att_error=%d", att_error);
        return att_error;
    }

    int result = 0;
    bool accepted = false;
    const uint8_t op = length > 4 ? control[4] : 0;
    uint32_t begin_size = 0;
    uint16_t begin_chunk = 0;
    uint16_t begin_window = 0;
    if (op == DENZIC_OTA_V1_OP_BEGIN && length >= DENZIC_OTA_V1_CONTROL_BYTES) {
        begin_size = (uint32_t)control[8]
            | ((uint32_t)control[9] << 8)
            | ((uint32_t)control[10] << 16)
            | ((uint32_t)control[11] << 24);
        begin_chunk = (uint16_t)control[12] | ((uint16_t)control[13] << 8);
        begin_window = (uint16_t)control[14] | ((uint16_t)control[15] << 8);
        ESP_LOGI(
            TAG,
            "Denzic OTA v1 control RX op=%s(%u) size=%u chunk=%u window=%u state=%u",
            ble_firmware_ota_op_name(op),
            op,
            (unsigned)begin_size,
            (unsigned)begin_chunk,
            (unsigned)begin_window,
            (unsigned)s_ota.state);
    } else if (op == DENZIC_OTA_V1_OP_BEGIN || op == DENZIC_OTA_V1_OP_FINISH ||
               op == DENZIC_OTA_V1_OP_ABORT) {
        ESP_LOGI(
            TAG,
            "Denzic OTA v1 control RX op=%s(%u) len=%u state=%u",
            ble_firmware_ota_op_name(op),
            op,
            length,
            (unsigned)s_ota.state);
    }
    ble_firmware_ota_lock();
    accepted = denzic_ota_v1_handle_control(&s_ota, control, length);
    if (accepted && op == DENZIC_OTA_V1_OP_SYNC) {
        ble_firmware_ota_drain_queued_data_locked();
    }
    ble_firmware_ota_update_active_link();
    if (!accepted) {
        result = ble_firmware_ota_att_error_from_core();
    }
    const uint8_t last_error = s_ota.last_error;
    const uint8_t state_after = s_ota.state;
    ble_firmware_ota_unlock();

    if (!accepted) {
        ESP_LOGW(
            TAG,
            "Denzic OTA v1 control REJECTED op=%s(%u) error=%u(%s) att=%d len=%u state=%u size=%u",
            ble_firmware_ota_op_name(op),
            op,
            last_error,
            ble_firmware_ota_error_name(last_error),
            result,
            length,
            (unsigned)state_after,
            (unsigned)begin_size);
    } else if (op == DENZIC_OTA_V1_OP_BEGIN) {
        ESP_LOGI(
            TAG,
            "Denzic OTA v1 BEGIN accepted size=%u chunk=%u window=%u state=%u",
            (unsigned)begin_size,
            (unsigned)begin_chunk,
            (unsigned)begin_window,
            (unsigned)state_after);
        /* BEGIN starts bulk WWR immediately. Request active CI/2M PHY now;
         * ble_hid_gap_request_active_connection() retains the bounded async
         * completion fallback and per-connection rejection backoff. */
        if (ble_hid_gap_schedule_active_connection != NULL ||
            ble_hid_gap_request_active_connection != NULL) {
            esp_err_t ret = ble_hid_gap_request_active_connection != NULL
                ? ble_hid_gap_request_active_connection()
                : ble_hid_gap_schedule_active_connection();
            ESP_LOGI(TAG, "Denzic OTA v1 BEGIN requested active BLE link ret=%s",
                     esp_err_to_name(ret));
        }
    } else if (op == DENZIC_OTA_V1_OP_FINISH || op == DENZIC_OTA_V1_OP_ABORT) {
        ESP_LOGI(
            TAG,
            "Denzic OTA v1 control accepted op=%s(%u) state=%u",
            ble_firmware_ota_op_name(op),
            op,
            (unsigned)state_after);
        if (op == DENZIC_OTA_V1_OP_FINISH && s_ota_worker_task != NULL) {
            ESP_LOGI(
                TAG,
                "Denzic OTA v1 worker stack high-water bytes=%u configured_bytes=%u",
                (unsigned)uxTaskGetStackHighWaterMark(s_ota_worker_task),
                (unsigned)BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES);
        }
    }
    return result;
}

static int ble_firmware_ota_handle_data_write(struct os_mbuf *om)
{
    ble_firmware_ota_data_job_t job;
    uint16_t length = 0;
    int att_error = ble_firmware_ota_copy_mbuf(om, job.data, sizeof(job.data), &length);
    if (att_error != 0) {
        /* copy 失败（过大的 GATT 写）。data 特征值同时声明 WRITE_NO_RSP，但超长写通常仍以
         * WriteWithResponse 走 ATT 错误回传；按原契约 abort 会话并回错误让 host 看到。 */
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        return att_error;
    }
    job.length = length;

    /* 修复4：data 不在 NimBLE 任务上同步落 flash，而是入队由 worker 处理。队列满时阻塞——
     * 这是安全的背压：NimBLE 停止接收新数据 → 控制器/链路层 LE Flow Control 让 host 减速，
     * 无丢包、镜像不损坏（砖机安全的根本）。2s 超时仅在 worker 卡死（不应发生）时触发，
     * 此时 abort 会话——安全降级而非砖机。data 是 WRITE_NO_RSP，host 不靠 per-write ATT
     * error 判成败（靠 status 特征值），故异步入队不破坏桌面端契约。 */
    if (xQueueSend(
            s_ota_worker_queue,
            &job,
            pdMS_TO_TICKS(BLE_FIRMWARE_OTA_DATA_ENQUEUE_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Denzic OTA v1 data enqueue timed out; aborting session");
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
    }
    return 0;
}

static const char *ble_firmware_ota_compact_capabilities_if_needed(
    uint16_t conn_handle,
    const char *value)
{
    if (value == NULL) {
        return "";
    }
    uint16_t mtu = ble_att_mtu(conn_handle);
    uint16_t value_max = mtu > 1 ? (uint16_t)(mtu - 1U) : 0U;
    if (mtu <= BLE_ATT_MTU_DFLT && strlen(value) > value_max) {
        return BLE_FIRMWARE_OTA_COMPACT_CAPABILITIES;
    }
    return value;
}

static int ble_firmware_ota_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)attr_handle;
    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ble_firmware_ota_gatt_attr_t attr = (ble_firmware_ota_gatt_attr_t)(uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_STATUS) {
            return ble_firmware_ota_append_status(ctxt->om);
        }

        const char *value = NULL;
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_READINESS) {
            value = listener_device_get_factory_readiness();
        } else if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES) {
            value = ble_firmware_ota_compact_capabilities_if_needed(
                conn_handle,
                listener_device_get_capabilities());
        } else {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        return os_mbuf_append(ctxt->om, value, strlen(value)) == 0
            ? 0
            : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL) {
        return ble_firmware_ota_handle_control_write(ctxt->om);
    }
    if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_DATA ||
        attr == BLE_FIRMWARE_OTA_GATT_ATTR_DATA_B) {
        return ble_firmware_ota_handle_data_write(ctxt->om);
    }
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def s_ota_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_readiness_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_READINESS,
            },
            {
                .uuid = &s_control_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_CONTROL,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_DATA,
            },
            {
                .uuid = &s_data_b_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_DATA_B,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_STATUS,
            },
            {
                .uuid = &s_capabilities_uuid.u,
                .access_cb = ble_firmware_ota_access,
                .flags = BLE_GATT_CHR_F_READ,
                .arg = (void *)(uintptr_t)BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES,
            },
            {0},
        },
    },
    {0},
};

esp_err_t ble_firmware_ota_register_gatt(void)
{
    if (s_registered) {
        return ESP_OK;
    }
    /* 修复1：firmware_ota 初始化失败（mutex 未建）则不注册 OTA GATT——OTA 不可达，
     * 从根上避免无锁裸奔。BLE 侧原语也不再创建。 */
    if (!firmware_ota_is_ready()) {
        ESP_LOGE(TAG, "Denzic OTA v1 GATT not registered: firmware_ota not ready");
        return ESP_ERR_INVALID_STATE;
    }

    s_ota_lock = xSemaphoreCreateMutex();
    s_ota_worker_queue = xQueueCreateStatic(
        BLE_FIRMWARE_OTA_WORKER_QUEUE_DEPTH,
        sizeof(ble_firmware_ota_data_job_t),
        s_ota_worker_queue_storage,
        &s_ota_worker_queue_control);
    if (s_ota_lock == NULL || s_ota_worker_queue == NULL) {
        ESP_LOGE(
            TAG,
            "Denzic OTA v1 worker primitives create failed queue_depth=%u job_bytes=%u",
            (unsigned)BLE_FIRMWARE_OTA_WORKER_QUEUE_DEPTH,
            (unsigned)sizeof(ble_firmware_ota_data_job_t));
        return ESP_ERR_NO_MEM;
    }

    ble_firmware_ota_core_init();

    /* ble_ota_worker 经 denzic_ota_v1_handle_data → firmware_ota_write → esp_ota_write
     * 写 flash，栈必须留片内：flash 操作期间 cache 关闭，PSRAM 栈访问会触发
     * esp_task_stack_is_sane_cache_disabled assert。静态 BSS 栈，不用 xTaskCreate。 */
    s_ota_worker_task = xTaskCreateStatic(
        ble_firmware_ota_worker_task,
        "ble_ota_worker",
        BLE_FIRMWARE_OTA_WORKER_TASK_STACK_BYTES,
        NULL,
        BLE_FIRMWARE_OTA_WORKER_TASK_PRIORITY,
        s_ota_worker_stack,
        &s_ota_worker_tcb);
    if (s_ota_worker_task == NULL) {
        ESP_LOGE(TAG, "Denzic OTA v1 worker static task create failed");
        return ESP_FAIL;
    }

    int rc = ble_gatts_count_cfg(s_ota_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_ota_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    s_registered = true;
    ESP_LOGI(
        TAG,
        "Denzic OTA v1 GATT driver registered (DATA+DATA_B, reorder_slots=%u)",
        (unsigned)BLE_FIRMWARE_OTA_REORDER_SLOTS);
    return ESP_OK;
}

void ble_firmware_ota_on_gap_disconnect(uint16_t conn_handle)
{
    (void)conn_handle;
    ble_firmware_ota_lock();
    bool receiving = (s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING);
    uint32_t bytes_written = (uint32_t)s_ota.bytes_written;
    uint32_t expected_size = (uint32_t)s_ota.expected_size;
    if (receiving) {
        s_active_link_retry_tick = 0;
        s_active_link_confirmed = false;
        denzic_ota_v1_set_status_flags(&s_ota, 0);
    }
    ble_firmware_ota_unlock();

    if (receiving) {
        ESP_LOGW(
            TAG,
            "Denzic OTA v1 link disconnected; preserving same-image session bytes=%u expected=%u until inactivity timeout",
            (unsigned)bytes_written,
            (unsigned)expected_size);
    }
}

void ble_firmware_ota_on_firmware_abort(uint32_t reason)
{
    /* 可能在任意任务（inactivity timer / USB / NimBLE control / worker data 处理）上被调用。
     * 不在此处直接 memset s_ota——那会与持锁的 data/control 处理竞争。只置位
     * s_core_reset_pending，worker 下一次循环（持锁）串行执行 core_init。可能在 worker 持锁
     * 处理 data 时被回调（storage_write 失败路径）——仅置位 volatile bool，不取锁，无死锁。
     * s_ota.state 读取仅为日志，容忍瞬时 staleness。 */
    if (s_ota.state != DENZIC_OTA_V1_STATE_IDLE) {
        ESP_LOGW(TAG, "Denzic OTA v1 session reset requested after firmware abort reason=%u", (unsigned)reason);
    }
    s_core_reset_pending = true;
}

void ble_firmware_ota_log_gatt_state(void)
{
    uint16_t service_handle = 0;
    uint16_t control_def_handle = 0;
    uint16_t control_val_handle = 0;
    uint16_t data_def_handle = 0;
    uint16_t data_val_handle = 0;
    uint16_t status_def_handle = 0;
    uint16_t status_val_handle = 0;
    int service_rc = ble_gatts_find_svc(&s_service_uuid.u, &service_handle);
    int control_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_control_uuid.u,
        &control_def_handle,
        &control_val_handle);
    int data_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_data_uuid.u,
        &data_def_handle,
        &data_val_handle);
    int status_rc = ble_gatts_find_chr(
        &s_service_uuid.u,
        &s_status_uuid.u,
        &status_def_handle,
        &status_val_handle);

    ESP_LOGI(
        TAG,
        "Denzic OTA v1 GATT state: registered=%u service_rc=%d service=%u control_rc=%d control_def=%u control_val=%u data_rc=%d data_def=%u data_val=%u status_rc=%d status_def=%u status_val=%u",
        s_registered,
        service_rc,
        service_handle,
        control_rc,
        control_def_handle,
        control_val_handle,
        data_rc,
        data_def_handle,
        data_val_handle,
        status_rc,
        status_def_handle,
        status_val_handle);
}
