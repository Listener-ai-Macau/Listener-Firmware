#include "ble_firmware_ota.h"
#include "ble_firmware_ota_reorder_bank.h"
#include "ble_firmware_ota_wire_esp32.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_capture.h"
#include "denzic_ota_v1.h"
#include "denzic_ota_v1_reorder.h"
#include "diag_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_private/esp_clk.h"
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

/* N16R8 uses the measured 40-write/80-fragment OTA pipeline. Defaults
 * alone do not override an existing sdkconfig; reject an effective stale pool
 * at compilation rather than shipping a different buffer contract again. */
#if defined(CONFIG_LISTENER_BOARD_PROFILE_V2_N16R8)
_Static_assert(CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT >= 128,
               "N16R8 dual-lane OTA requires at least 128 host RX ACL buffers");
_Static_assert(CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL == 1,
               "N16R8 OTA RX buffers must use external host memory");
#endif

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
 * 回调只作废内部job代次并请求reset，不反向取得端口锁。端口在writer完整事务
 * 屏障内结算旧reset后才能接受新BEGIN，已取出队列的旧job仍按代次拒绝。 */
typedef struct {
    uint16_t length;
    uint8_t data[BLE_FIRMWARE_OTA_DATA_MAX_BYTES];
    uint32_t enqueue_started_us;
    uint32_t owner_epoch;
} ble_firmware_ota_data_job_t;

/* Bounded counters only, reset under the existing BEGIN owner. No per-DATA
 * logs or timers: observe callback backpressure versus worker/Flash service.
 * uint32 clock differences cover this protocol's bounded transfer lifetime. */
typedef struct {
    uint32_t received;
    uint32_t processed;
    uint32_t callback_drained;
    uint32_t first_arrival_us;
    uint32_t last_arrival_us;
    uint32_t max_arrival_gap_us;
    uint32_t max_queue_depth;
    uint64_t enqueue_total_us;
    uint32_t enqueue_max_us;
    uint64_t residence_total_us;
    uint32_t residence_max_us;
    uint64_t service_total_us;
    uint32_t service_max_us;
    uint64_t worker_lock_total_us;
    uint32_t worker_lock_max_us;
} ble_firmware_ota_timing_t;

static portMUX_TYPE s_ota_timing_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_firmware_ota_timing_t s_ota_timing;

static void ble_firmware_ota_timing_reset(void)
{
    portENTER_CRITICAL(&s_ota_timing_lock);
    memset(&s_ota_timing, 0, sizeof(s_ota_timing));
    portEXIT_CRITICAL(&s_ota_timing_lock);
}

static void ble_firmware_ota_timing_print(void)
{
    portENTER_CRITICAL(&s_ota_timing_lock);
    ble_firmware_ota_timing_t timing = s_ota_timing;
    portEXIT_CRITICAL(&s_ota_timing_lock);
    ESP_LOGI("ble_firmware_ota",
        "OTA queue timing received=%u processed=%u callback_drained=%u arrival_span_us=%u arrival_gap_max_us=%u queue_depth_max=%u enqueue_us=%llu enqueue_max_us=%u residence_us=%llu residence_max_us=%u service_us=%llu service_max_us=%u worker_lock_us=%llu worker_lock_max_us=%u",
        (unsigned)timing.received, (unsigned)timing.processed,
        (unsigned)timing.callback_drained,
        (unsigned)(timing.last_arrival_us - timing.first_arrival_us),
        (unsigned)timing.max_arrival_gap_us, (unsigned)timing.max_queue_depth,
        (unsigned long long)timing.enqueue_total_us, (unsigned)timing.enqueue_max_us,
        (unsigned long long)timing.residence_total_us, (unsigned)timing.residence_max_us,
        (unsigned long long)timing.service_total_us, (unsigned)timing.service_max_us,
        (unsigned long long)timing.worker_lock_total_us, (unsigned)timing.worker_lock_max_us);
}

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
static volatile bool s_prepare_link_active;
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
static ble_firmware_ota_reorder_bank_t s_ota_reorder_bank = {
    .target = &s_ota_reorder,
    .base = s_ota_reorder_slots,
    .base_count = BLE_FIRMWARE_OTA_REORDER_SLOTS,
    .max_payload = BLE_FIRMWARE_OTA_DATA_PAYLOAD_MAX,
};

static void *ble_firmware_ota_allocate_reorder(size_t count, size_t size)
{
    /* Never borrow boot/recording internal RAM for a larger send window. */
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool ble_firmware_ota_reserve_reorder_window(uint16_t window)
{
    if (!ble_firmware_ota_reorder_bank_reserve(
            &s_ota_reorder_bank, window,
            ble_firmware_ota_allocate_reorder, heap_caps_free)) {
        ESP_LOGW(TAG, "OTA reorder window allocation failed slots=%u bytes=%u; current slots=%u retained",
                 (unsigned)window,
                 (unsigned)((size_t)window * sizeof(denzic_ota_v1_reorder_slot_t)),
                 (unsigned)s_ota_reorder.slot_count);
        return false;
    }
    return true;
}
/* Allocate only for an active OTA, after boot-time audio DMA owns its memory.
 * Flash cache-disabled writes still require this payload to stay internal. */
static uint8_t *s_ota_storage_batch;
static size_t s_ota_storage_batch_length;
static uint32_t s_ota_storage_write_calls;
static uint64_t s_ota_storage_write_total_us;
static uint32_t s_ota_storage_write_max_us;
static ble_firmware_ota_wire_t *s_ota_wire;
/* Requested encoding belongs to the current control call. Active encoding
 * belongs to the storage owner, including a partial block on same-image resume. */
static uint8_t s_ota_requested_encoding;
static uint32_t s_ota_requested_raw_size;
static uint8_t s_ota_active_encoding;
static uint32_t s_ota_active_raw_size;

static bool ble_firmware_ota_decoded_write(void *context, const uint8_t *data, size_t length);

static void ble_firmware_ota_release_wire(void)
{
    ble_firmware_ota_wire_free(s_ota_wire);
    s_ota_wire = NULL;
    s_ota_active_encoding = 0;
    s_ota_active_raw_size = 0;
}
/* Abort also runs outside the port lock. Epoch/reset share a short critical
 * section with enqueue: a worker can already hold a dequeued old item while a
 * new BEGIN owns the core. Never infer ownership from queue draining alone. */
static portMUX_TYPE s_ota_owner_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ota_owner_epoch;
static bool s_core_reset_pending;
static bool s_advertise_deflate = true;
static void ble_firmware_ota_settle_reset_locked(void);

static uint32_t ble_firmware_ota_job_epoch(void)
{
    portENTER_CRITICAL(&s_ota_owner_lock);
    uint32_t epoch = s_ota_owner_epoch;
    portEXIT_CRITICAL(&s_ota_owner_lock);
    return epoch;
}

static void ble_firmware_ota_begin_owner_locked(void)
{
    /* Called only by storage.begin: the core is already replacing its old
     * image. The transaction barrier has completed that old writer's entire
     * abort, including its notification. This cannot clear a later abort. */
    portENTER_CRITICAL(&s_ota_owner_lock);
    ++s_ota_owner_epoch;
    s_core_reset_pending = false;
    portEXIT_CRITICAL(&s_ota_owner_lock);
}

extern esp_err_t ble_hid_gap_request_active_connection(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_schedule_active_connection(void) __attribute__((weak));
extern bool ble_hid_gap_active_connection_applied(void) __attribute__((weak));
extern bool ble_hid_gap_ota_connection_ready(void) __attribute__((weak));
extern bool ble_hid_gap_get_ota_rx_budget(uint16_t conn_handle, uint16_t *octets,
                                         uint16_t *time_us, uint8_t *phy) __attribute__((weak));
extern esp_err_t ble_hid_gap_prepare_shutdown_disconnect(void) __attribute__((weak));
extern bool ble_hid_gap_is_connected(void) __attribute__((weak));
extern esp_err_t ble_hid_gap_set_ota_tx_power(bool enabled) __attribute__((weak));

static void ble_firmware_ota_set_transfer_tx_power(bool active)
{
    if (ble_hid_gap_set_ota_tx_power != NULL) {
        (void)ble_hid_gap_set_ota_tx_power(active);
    }
}

static void ble_firmware_ota_prepare_link(void)
{
    s_prepare_link_active = true;
    if (ble_hid_gap_schedule_active_connection != NULL ||
        ble_hid_gap_request_active_connection != NULL) {
        (void)(ble_hid_gap_schedule_active_connection != NULL
            ? ble_hid_gap_schedule_active_connection()
            : ble_hid_gap_request_active_connection());
    }
}

static void ble_firmware_ota_lock(void)
{
    if (s_ota_lock != NULL) {
        xSemaphoreTake(s_ota_lock, portMAX_DELAY);
    }
    const bool guarded = firmware_ota_transaction_lock();
    configASSERT(guarded); /* GATT registration requires both initialized locks. */
    ble_firmware_ota_settle_reset_locked();
}

static void ble_firmware_ota_unlock(void)
{
    firmware_ota_transaction_unlock();
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

uint32_t firmware_ota_platform_cpu_mhz(void)
{
    return (uint32_t)(esp_clk_cpu_freq() / 1000000);
}

static bool ble_firmware_ota_storage_begin(void *driver_context, uint32_t image_size)
{
    (void)driver_context;
    ble_firmware_ota_begin_owner_locked();
    ble_firmware_ota_release_wire();
    ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
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
    /* The core has validated BEGIN and installed its negotiated window before
     * invoking this driver. Reserve the full window only after audio owns its
     * boot/DMA resources. On failure BEGIN acquires no Flash image owner. */
    if (!ble_firmware_ota_reserve_reorder_window(s_ota.window_chunks)) {
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
        ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
        (void)audio_capture_set_ota_suspended(false);
        return false;
    }
    denzic_ota_v1_reorder_clear(&s_ota_reorder);
    ESP_LOGI(TAG, "OTA reorder window reserved slots=%u bytes=%u external=%u",
             (unsigned)s_ota_reorder.slot_count,
             (unsigned)(s_ota_reorder.slot_count * sizeof(denzic_ota_v1_reorder_slot_t)),
             s_ota_reorder_bank.owned != NULL ? 1U : 0U);
    uint32_t raw_size = image_size;
    if (s_ota_requested_encoding == 1) {
        raw_size = s_ota_requested_raw_size;
        s_ota_wire = ble_firmware_ota_wire_create(image_size, raw_size,
                                                 ble_firmware_ota_decoded_write);
        if (s_ota_wire == NULL) {
            heap_caps_free(s_ota_storage_batch);
            s_ota_storage_batch = NULL;
            ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
            (void)audio_capture_set_ota_suspended(false);
            return false;
        }
    }
    esp_err_t ret = firmware_ota_begin(raw_size, DENZIC_OTA_V1_PROTOCOL_NAME);
    if (ret == ESP_OK) {
        s_ota_active_encoding = s_ota_requested_encoding;
        s_ota_active_raw_size = raw_size;
        ble_firmware_ota_timing_reset();
        s_prepare_link_active = false;
        ble_firmware_ota_set_transfer_tx_power(true);
    } else {
        ble_firmware_ota_release_wire();
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
        ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
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

static bool ble_firmware_ota_raw_write(
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

static bool ble_firmware_ota_decoded_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    /* Keep every actual Flash payload internal and preserve the existing
     * batching/erase-ahead path. Never hand a whole decoded block to Flash. */
    while (length != 0) {
        size_t take = BLE_FIRMWARE_OTA_STORAGE_BATCH_BYTES - s_ota_storage_batch_length;
        if (take > length) take = length;
        if (!ble_firmware_ota_raw_write(NULL, data, take)) return false;
        data += take;
        length -= take;
    }
    return true;
}

static bool ble_firmware_ota_storage_write(void *context, const uint8_t *data, size_t length)
{
    if (s_ota_active_encoding == 0) return ble_firmware_ota_raw_write(context, data, length);
    if (s_ota_wire != NULL && firmware_ota_wire_write(&s_ota_wire->wire, data, length)) return true;
    /* Malformed/failed decode may have written a prefix but must never commit
     * it or resume a parser after it has partially consumed a failed packet. */
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
    return false;
}

static bool ble_firmware_ota_storage_finish(void *driver_context)
{
    (void)driver_context;
    if (s_ota_active_encoding != 0 &&
        (s_ota_wire == NULL || !firmware_ota_wire_finish(&s_ota_wire->wire))) {
        firmware_ota_abort(DIAG_OTA_ABORT_BLE_WRITE_FAIL);
        ble_firmware_ota_release_wire();
        return false;
    }
    if (!ble_firmware_ota_storage_flush()) {
        heap_caps_free(s_ota_storage_batch);
        s_ota_storage_batch = NULL;
        ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
        ble_firmware_ota_release_wire();
        return false;
    }
    if (s_ota_wire != NULL) {
        ESP_LOGI(TAG, "OTA decoded wire_bytes=%u raw_bytes=%u blocks=%u inflate_total_us=%llu inflate_max_us=%u codec_bytes=%u",
            (unsigned)s_ota_wire->wire.wire_received, (unsigned)s_ota_wire->wire.raw_written,
            (unsigned)s_ota_wire->wire.blocks, (unsigned long long)s_ota_wire->inflate_us,
            (unsigned)s_ota_wire->inflate_max_us, (unsigned)sizeof(*s_ota_wire));
    }
    esp_err_t ret = firmware_ota_finish(false);
    ble_firmware_ota_release_wire();
    ble_firmware_ota_timing_print();
    ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
    s_prepare_link_active = false;
    ble_firmware_ota_set_transfer_tx_power(false);
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
    ble_firmware_ota_release_wire();
    s_ota_storage_batch_length = 0;
    heap_caps_free(s_ota_storage_batch);
    s_ota_storage_batch = NULL;
    ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
    firmware_ota_abort(DIAG_OTA_ABORT_BLE_CONTROL);
    s_prepare_link_active = false;
    ble_firmware_ota_set_transfer_tx_power(false);
    (void)audio_capture_set_ota_suspended(false);
}

static void ble_firmware_ota_core_init(void)
{
    ble_firmware_ota_release_wire();
    s_ota_requested_encoding = 0;
    s_ota_requested_raw_size = 0;
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
    ble_firmware_ota_reorder_bank_release(&s_ota_reorder_bank, heap_caps_free);
    s_active_link_retry_tick = 0;
    s_active_link_confirmed = false;
}

static void ble_firmware_ota_settle_reset_locked(void)
{
    portENTER_CRITICAL(&s_ota_owner_lock);
    bool pending = s_core_reset_pending;
    s_core_reset_pending = false;
    portEXIT_CRITICAL(&s_ota_owner_lock);
    if (!pending) {
        return;
    }
    ble_firmware_ota_data_job_t stale;
    while (xQueueReceive(s_ota_worker_queue, &stale, 0) == pdTRUE) {
        /* The old writer is fully retired before a new BEGIN is possible. */
    }
    ble_firmware_ota_core_init();
    (void)audio_capture_set_ota_suspended(false);
}

static bool ble_firmware_ota_tick_reached(TickType_t now, TickType_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void ble_firmware_ota_update_active_link(void)
{
    if (s_ota.state != DENZIC_OTA_V1_STATE_RECEIVING && !s_prepare_link_active) {
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
        s_active_link_confirmed = true;
        denzic_ota_v1_set_status_flags(
            &s_ota,
            DENZIC_OTA_V1_STATUS_FLAG_ACTIVE_LINK_CONFIRMED);
    } else {
        s_active_link_confirmed = false;
        denzic_ota_v1_set_status_flags(&s_ota, 0);
    }

    if (transfer_link_ready) {
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
        (void)(ble_hid_gap_schedule_active_connection != NULL
            ? ble_hid_gap_schedule_active_connection()
            : ble_hid_gap_request_active_connection());
    }
}

bool ble_firmware_ota_is_deflate_advertised(void)
{
    portENTER_CRITICAL(&s_ota_owner_lock);
    bool enabled = s_advertise_deflate;
    portEXIT_CRITICAL(&s_ota_owner_lock);
    return enabled;
}

bool ble_firmware_ota_set_deflate_advertisement(bool enabled)
{
    ble_firmware_ota_lock();
    firmware_ota_status_t status = firmware_ota_get_status();
    bool idle = !status.active && !status.pending_verify &&
        s_ota.state != DENZIC_OTA_V1_STATE_RECEIVING &&
        s_ota.state != DENZIC_OTA_V1_STATE_ERASING;
    if (idle) {
        portENTER_CRITICAL(&s_ota_owner_lock);
        s_advertise_deflate = enabled;
        portEXIT_CRITICAL(&s_ota_owner_lock);
    }
    ble_firmware_ota_unlock();
    return idle;
}

static void ble_firmware_ota_sync_status(void)
{
    ble_firmware_ota_update_active_link();
    firmware_ota_status_t status = firmware_ota_get_status();
    if (s_ota_active_encoding == 0 &&
        s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING && status.active) {
        s_ota.bytes_written = (uint32_t)status.bytes_written;
        s_ota.expected_size = (uint32_t)status.expected_size;
    }
}

/* 修复4 + 修复1：OTA worker 任务。data 写入（含 flash）在此任务上执行，不阻塞 NimBLE host
 * 任务；reset（core_init）也在此任务上串行执行。100ms 轮询超时既能在 idle 时及时服务 abort
 * 请求，又能定期喂看门狗（若用 portMAX_DELAY 则无数据时永不喂狗→触发 WDT）。 */
static void ble_firmware_ota_process_job_locked(
    const ble_firmware_ota_data_job_t *job, bool callback_drain)
{
    portENTER_CRITICAL(&s_ota_owner_lock);
    bool current = !s_core_reset_pending && job->owner_epoch == s_ota_owner_epoch;
    portEXIT_CRITICAL(&s_ota_owner_lock);
    if (!current) {
        return;
    }
    const uint32_t start = (uint32_t)esp_timer_get_time();
    const uint32_t residence = start - job->enqueue_started_us;
    (void)denzic_ota_v1_handle_data_ordered(
        &s_ota, &s_ota_reorder, job->data, job->length);
    const uint32_t service = (uint32_t)esp_timer_get_time() - start;
    portENTER_CRITICAL(&s_ota_timing_lock);
    s_ota_timing.processed++;
    s_ota_timing.callback_drained += callback_drain ? 1U : 0U;
    s_ota_timing.residence_total_us += residence;
    if (residence > s_ota_timing.residence_max_us) s_ota_timing.residence_max_us = residence;
    s_ota_timing.service_total_us += service;
    if (service > s_ota_timing.service_max_us) s_ota_timing.service_max_us = service;
    portEXIT_CRITICAL(&s_ota_timing_lock);
}

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

        const uint32_t lock_started = (uint32_t)esp_timer_get_time();
        ble_firmware_ota_lock();
        if (have_job) {
            const uint32_t waited = (uint32_t)esp_timer_get_time() - lock_started;
            portENTER_CRITICAL(&s_ota_timing_lock);
            s_ota_timing.worker_lock_total_us += waited;
            if (waited > s_ota_timing.worker_lock_max_us) s_ota_timing.worker_lock_max_us = waited;
            portEXIT_CRITICAL(&s_ota_timing_lock);
        }
        if (have_job) {
            /* Batch-drain while holding the lock so flash stays busy and host
             * SYNC/status sees a fresher offset (fewer recoveries / resends). */
            do {
                ble_firmware_ota_process_job_locked(&job, false);
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
        ble_firmware_ota_process_job_locked(&job, true);
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
    uint8_t control[DENZIC_OTA_V1_CONTROL_BYTES + 4];
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
    uint32_t begin_crc = 0;
    uint8_t begin_encoding = 0;
    uint32_t begin_raw_size = 0;
    if (op == DENZIC_OTA_V1_OP_BEGIN && length >= DENZIC_OTA_V1_CONTROL_BYTES) {
        begin_size = (uint32_t)control[8]
            | ((uint32_t)control[9] << 8)
            | ((uint32_t)control[10] << 16)
            | ((uint32_t)control[11] << 24);
        begin_chunk = (uint16_t)control[12] | ((uint16_t)control[13] << 8);
        begin_window = (uint16_t)control[14] | ((uint16_t)control[15] << 8);
        begin_crc = (uint32_t)control[16] | ((uint32_t)control[17] << 8)
            | ((uint32_t)control[18] << 16) | ((uint32_t)control[19] << 24);
        begin_encoding = control[6];
        if (control[7] != 0 ||
            (begin_encoding == 0 ? length != DENZIC_OTA_V1_CONTROL_BYTES :
             begin_encoding != 1 || length != DENZIC_OTA_V1_CONTROL_BYTES + 4)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        begin_raw_size = begin_size;
        if (begin_encoding == 1) {
            begin_raw_size = (uint32_t)control[20] | ((uint32_t)control[21] << 8) |
                ((uint32_t)control[22] << 16) | ((uint32_t)control[23] << 24);
            if (!firmware_ota_wire_sizes_valid(begin_size, begin_raw_size)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
        }
    }
    if (op == DENZIC_OTA_V1_OP_BEGIN &&
        ble_hid_gap_ota_connection_ready != NULL &&
        !ble_hid_gap_ota_connection_ready()) {
        esp_err_t ret = ble_hid_gap_request_active_connection != NULL
            ? ble_hid_gap_request_active_connection()
            : (ble_hid_gap_schedule_active_connection != NULL
                ? ble_hid_gap_schedule_active_connection()
                : ESP_ERR_NOT_SUPPORTED);
        ESP_LOGW(
            TAG,
            "Denzic OTA v1 BEGIN deferred until full-speed BLE link ret=%s",
            esp_err_to_name(ret));
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    ble_firmware_ota_lock();
    if (op == DENZIC_OTA_V1_OP_BEGIN) {
        /* The shared core's resume key is wire size/CRC. It cannot distinguish
         * encoded and raw interpretation: enforce that identity before resume. */
        if (s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING &&
            begin_size == s_ota.expected_size && begin_crc == s_ota.image_crc32 &&
            (begin_encoding != s_ota_active_encoding ||
             begin_raw_size != s_ota_active_raw_size)) {
            ble_firmware_ota_unlock();
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_ota_requested_encoding = begin_encoding;
        s_ota_requested_raw_size = begin_raw_size;
    }
    /* Same-image BEGIN resumes without invoking storage.begin. Grow before the
     * core changes its window; allocation failure leaves its old contract and
     * pending raw packets untouched. All other BEGIN validation stays in core. */
    static const uint8_t begin_magic[4] = DENZIC_OTA_V1_MAGIC;
    if (op == DENZIC_OTA_V1_OP_BEGIN && length >= DENZIC_OTA_V1_CONTROL_BYTES &&
        memcmp(control, begin_magic, sizeof(begin_magic)) == 0 &&
        control[5] == DENZIC_OTA_V1_PROTOCOL_VERSION &&
        begin_chunk > 0 && begin_chunk <= s_ota.max_chunk_payload_bytes &&
        s_ota.state == DENZIC_OTA_V1_STATE_RECEIVING &&
        begin_size == s_ota.expected_size && begin_crc == s_ota.image_crc32 &&
        !ble_firmware_ota_reserve_reorder_window(
            begin_window != 0 ? begin_window : s_ota.default_window_chunks)) {
        ble_firmware_ota_unlock();
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (op == DENZIC_OTA_V1_OP_SYNC) {
        /* DATA uses a worker queue. A fast link can deliver SYNC while the
         * tail is still queued; drain it before core validation so the
         * confirmed offset cannot falsely lag and force a host rewind. */
        ble_firmware_ota_drain_queued_data_locked();
    }
    accepted = denzic_ota_v1_handle_control(&s_ota, control, length);
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
        /* BEGIN starts bulk WWR immediately. Refresh the active profile while
         * the host retains its firmware-update throughput request. */
        if (ble_hid_gap_schedule_active_connection != NULL ||
            ble_hid_gap_request_active_connection != NULL) {
            (void)(ble_hid_gap_request_active_connection != NULL
                ? ble_hid_gap_request_active_connection()
                : ble_hid_gap_schedule_active_connection());
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
    job.owner_epoch = ble_firmware_ota_job_epoch();

    /* 修复4：data 不在 NimBLE 任务上同步落 flash，而是入队由 worker 处理。队列满时阻塞——
     * 这是安全的背压：NimBLE 停止接收新数据 → 控制器/链路层 LE Flow Control 让 host 减速，
     * 无丢包、镜像不损坏（砖机安全的根本）。2s 超时仅在 worker 卡死（不应发生）时触发，
     * 此时 abort 会话——安全降级而非砖机。data 是 WRITE_NO_RSP，host 不靠 per-write ATT
     * error 判成败（靠 status 特征值），故异步入队不破坏桌面端契约。 */
    job.enqueue_started_us = (uint32_t)esp_timer_get_time();
    const uint32_t depth_before = uxQueueMessagesWaiting(s_ota_worker_queue);
    const BaseType_t enqueued = xQueueSend(
        s_ota_worker_queue, &job,
        pdMS_TO_TICKS(BLE_FIRMWARE_OTA_DATA_ENQUEUE_TIMEOUT_MS));
    const uint32_t elapsed = (uint32_t)esp_timer_get_time() - job.enqueue_started_us;
    portENTER_CRITICAL(&s_ota_timing_lock);
    if (s_ota_timing.received == 0) {
        s_ota_timing.first_arrival_us = job.enqueue_started_us;
    } else {
        const uint32_t gap = job.enqueue_started_us - s_ota_timing.last_arrival_us;
        if (gap > s_ota_timing.max_arrival_gap_us) s_ota_timing.max_arrival_gap_us = gap;
    }
    s_ota_timing.received++;
    s_ota_timing.last_arrival_us = job.enqueue_started_us;
    if (depth_before > s_ota_timing.max_queue_depth) s_ota_timing.max_queue_depth = depth_before;
    s_ota_timing.enqueue_total_us += elapsed;
    if (elapsed > s_ota_timing.enqueue_max_us) s_ota_timing.enqueue_max_us = elapsed;
    portEXIT_CRITICAL(&s_ota_timing_lock);
    if (enqueued != pdTRUE) {
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
            ble_firmware_ota_prepare_link();
            value = listener_device_get_factory_readiness();
        } else if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_CAPABILITIES) {
            ble_firmware_ota_prepare_link();
            value = ble_firmware_ota_compact_capabilities_if_needed(
                conn_handle,
                listener_device_get_capabilities());
        } else {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        if (os_mbuf_append(ctxt->om, value, strlen(value)) != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_READINESS &&
            ble_hid_gap_get_ota_rx_budget != NULL) {
            uint16_t octets = 0;
            uint16_t time_us = 0;
            uint8_t phy = 0;
            if (ble_hid_gap_get_ota_rx_budget(conn_handle, &octets, &time_us, &phy)) {
                /* Optional extension: old hosts ignore it. Never append cached
                 * geometry from another connection or change binary status. */
                char budget[112];
                int size = snprintf(budget, sizeof(budget),
                    ";ota_rx_budget_v1=1;ota_rx_octets=%u;ota_rx_time_us=%u;ota_rx_phy=%u",
                    (unsigned)octets, (unsigned)time_us, (unsigned)phy);
                if (size <= 0 || size >= sizeof(budget) ||
                    strlen(value) + (size_t)size > BLE_FIRMWARE_OTA_DATA_MAX_BYTES ||
                    os_mbuf_append(ctxt->om, budget, size) != 0) {
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                }
            }
        }
        if (attr == BLE_FIRMWARE_OTA_GATT_ATTR_READINESS &&
            ble_firmware_ota_is_deflate_advertised()) {
            static const char codec[] = ";ota_deflate_blocks_v1=1";
            if (OS_MBUF_PKTLEN(ctxt->om) + sizeof(codec) - 1 > BLE_FIRMWARE_OTA_DATA_MAX_BYTES ||
                os_mbuf_append(ctxt->om, codec, sizeof(codec) - 1) != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
        }
        return 0;
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
        "Denzic OTA v1 GATT driver registered (DATA+DATA_B, reorder_slots=%u) sync_predrain=1",
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
    s_prepare_link_active = false;
    ble_firmware_ota_set_transfer_tx_power(false);
    /* Runs within the writer transaction, including timer/USB callers. Do not
     * acquire the port lock here: worker storage failures can invoke us while
     * already holding it. New BEGIN cannot enter until this publication and
     * all writer runtime/timer cleanup have completed. */
    portENTER_CRITICAL(&s_ota_owner_lock);
    ++s_ota_owner_epoch;
    s_core_reset_pending = true;
    portEXIT_CRITICAL(&s_ota_owner_lock);
    ESP_LOGW(TAG, "Denzic OTA v1 session reset requested after firmware abort reason=%u", (unsigned)reason);
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
