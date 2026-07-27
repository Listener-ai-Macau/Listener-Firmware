#include "diag_log_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "denzic_diag_log_store.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "watchdog_platform.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "diag_log";

#define DIAG_LOG_SECTOR_SIZE 4096U
#define DIAG_LOG_WRITE_QUEUE_DEPTH 64U
#define DIAG_LOG_WRITER_STACK_SIZE 3072U
#define DIAG_LOG_WRITER_PRIORITY (tskIDLE_PRIORITY + 1U)

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_write_queue;
static denzic_diag_log_store_t s_store;
static bool s_store_ready;
static bool s_input_debug_enabled;
static portMUX_TYPE s_input_debug_lock = portMUX_INITIALIZER_UNLOCKED;
/* Writer touches flash; stack must stay internal DRAM (static BSS), never xTaskCreate
 * under SPIRAM_USE_MALLOC (large/fragmented heaps fall into PSRAM). */
static StaticTask_t s_diag_log_writer_tcb;
static StackType_t s_diag_log_writer_stack[DIAG_LOG_WRITER_STACK_SIZE];

static void diag_log_platform_writer_task(void *arg);

bool diag_log_input_debug_enabled(void)
{
    portENTER_CRITICAL(&s_input_debug_lock);
    bool enabled = s_input_debug_enabled;
    portEXIT_CRITICAL(&s_input_debug_lock);
    return enabled;
}

void diag_log_set_input_debug_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_input_debug_lock);
    s_input_debug_enabled = enabled;
    portEXIT_CRITICAL(&s_input_debug_lock);
    ESP_LOGW(TAG, "DIAGLOG INPUTDBG: %s", enabled ? "ON" : "OFF");
}

static bool diag_port_storage_read(void *context, uint32_t offset, uint8_t *buffer, size_t length)
{
    (void)context;
    return s_partition != NULL
        && esp_partition_read(s_partition, offset, buffer, length) == ESP_OK;
}

static bool diag_port_storage_write(void *context, uint32_t offset, const uint8_t *data, size_t length)
{
    (void)context;
    return s_partition != NULL
        && esp_partition_write(s_partition, offset, data, length) == ESP_OK;
}

static bool diag_port_storage_erase(void *context, uint32_t offset, size_t length)
{
    (void)context;
    return s_partition != NULL
        && esp_partition_erase_range(s_partition, offset, length) == ESP_OK;
}

static uint32_t diag_port_timestamp_ms(void *context)
{
    (void)context;
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static void diag_port_lock(void *context)
{
    (void)context;
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void diag_port_unlock(void *context)
{
    (void)context;
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void diag_port_log(void *context, denzic_diag_log_store_log_level_t level, const char *message)
{
    (void)context;
    switch (level) {
    case DENZIC_DIAG_LOG_STORE_LOG_WARN:
        ESP_LOGW(TAG, "%s", message);
        break;
    case DENZIC_DIAG_LOG_STORE_LOG_ERROR:
        ESP_LOGE(TAG, "%s", message);
        break;
    default:
        ESP_LOGI(TAG, "%s", message);
        break;
    }
}

static void diag_port_emit_line(void *context, const char *line)
{
    (void)context;
    printf("%s\n", line);
}

static void diag_port_pace(void *context)
{
    (void)context;
    fflush(stdout);
    watchdog_platform_feed_current_task();
    vTaskDelay(1);
}

static const char *diag_port_source_name(void *context, uint16_t source)
{
    (void)context;
    switch (source) {
    case 0x01: return "system";
    case 0x02: return "keyboard";
    case 0x03: return "ble_hid";
    case 0x04: return "ble_gap";
    case 0x05: return "audio";
    case 0x06: return "voice_rec";
    case 0x07: return "voice_key";
    case 0x08: return "self_test";
    case 0x09: return "health";
    case 0x0A: return "ble_audio";
    case 0x0B: return "ota";
    case 0x0C: return "power";
    case 0x0D: return "board";
    case 0x0E: return "status_led";
    default:   return "unknown";
    }
}

void diag_log_platform_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           ESP_PARTITION_SUBTYPE_DATA_FAT,
                                           "diag_log");
    if (s_partition == NULL) {
        ESP_LOGW(TAG, "diag_log partition not found");
        return;
    }

    ESP_LOGI(TAG, "diag_log partition: %uKB %u sectors",
             (unsigned)(s_partition->size / 1024),
             (unsigned)(s_partition->size / DIAG_LOG_SECTOR_SIZE));

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    const denzic_diag_log_store_port_t port = {
        .storage_read = diag_port_storage_read,
        .storage_write = diag_port_storage_write,
        .storage_erase = diag_port_storage_erase,
        .timestamp_ms = diag_port_timestamp_ms,
        .lock = diag_port_lock,
        .unlock = diag_port_unlock,
        .log = diag_port_log,
        .emit_line = diag_port_emit_line,
        .pace = diag_port_pace,
        .source_name = diag_port_source_name,
    };
    if (!denzic_diag_log_store_init(&s_store, &port, NULL, s_partition->size)) {
        ESP_LOGE(TAG, "diag log store init failed");
        return;
    }
    s_store_ready = true;

    s_write_queue = xQueueCreate(DIAG_LOG_WRITE_QUEUE_DEPTH, sizeof(denzic_diag_log_event_t));
    if (s_write_queue == NULL) {
        ESP_LOGE(TAG, "persistent writer queue create failed");
        return;
    }
    /* diag_log_writer 直接写/擦 flash（esp_partition_write/erase_range），
     * 栈必须留片内：flash 操作期间 cache 被关闭，PSRAM 栈访问会触发
     * esp_task_stack_is_sane_cache_disabled assert。静态 BSS 栈，不用 xTaskCreate。 */
    if (xTaskCreateStatic(
            diag_log_platform_writer_task,
            "diag_log_writer",
            DIAG_LOG_WRITER_STACK_SIZE,
            NULL,
            DIAG_LOG_WRITER_PRIORITY,
            s_diag_log_writer_stack,
            &s_diag_log_writer_tcb) == NULL) {
        ESP_LOGE(TAG, "persistent writer static task create failed");
        vQueueDelete(s_write_queue);
        s_write_queue = NULL;
        return;
    }

    ESP_LOGI(TAG, "diag_log ready: write_sector=%u write_offset=%u retained=%" PRIu32,
             (unsigned)s_store.write_sector, (unsigned)s_store.write_offset,
             s_store.retained_events);
}

static void diag_log_platform_writer_task(void *arg)
{
    (void)arg;

    denzic_diag_log_event_t evt;
    for (;;) {
        if (xQueueReceive(s_write_queue, &evt, portMAX_DELAY) == pdTRUE) {
            denzic_diag_log_store_write_event(&s_store, &evt);
        }
    }
}

void diag_log_platform_write(
    uint16_t source,
    uint8_t event,
    uint8_t severity,
    uint32_t arg1,
    uint32_t arg2,
    uint32_t arg3,
    uint32_t arg4)
{
    if (!s_store_ready) {
        return;
    }

    const denzic_diag_log_event_t evt = {
        .timestamp_ms = diag_port_timestamp_ms(NULL),
        .source = source,
        .event = event,
        .severity = severity,
        .arg1 = arg1,
        .arg2 = arg2,
        .arg3 = arg3,
        .arg4 = arg4,
    };

    if (s_write_queue != NULL &&
        xQueueSend(s_write_queue, &evt, 0) == pdTRUE) {
        return;
    }

    /* Preserve diagnostics if the async writer is unavailable; normal recovery never blocks on flash. */
    if (s_write_queue == NULL) {
        denzic_diag_log_store_write_event(&s_store, &evt);
    }
}

uint32_t diag_log_platform_count(void)
{
    return denzic_diag_log_store_count(&s_store);
}

void diag_log_platform_dump(void)
{
    if (!s_store_ready) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }
    denzic_diag_log_store_dump(&s_store);
}

void diag_log_platform_dump_last(uint32_t count)
{
    if (!s_store_ready) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }
    denzic_diag_log_store_dump_last(&s_store, count);
}

void diag_log_platform_dump_last_by_source(uint32_t count, uint16_t source)
{
    if (!s_store_ready) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }
    denzic_diag_log_store_dump_last_by_source(&s_store, count, source);
}

void diag_log_platform_clear(void)
{
    if (!s_store_ready) {
        return;
    }
    denzic_diag_log_store_clear(&s_store);
}

bool diag_log_platform_is_dumping(void)
{
    return denzic_diag_log_store_is_dumping(&s_store);
}

uint32_t diag_log_platform_read_range(uint32_t offset, uint32_t limit,
                                       void *buffer, uint32_t buffer_size)
{
    return denzic_diag_log_store_read_range(&s_store, offset, limit, buffer, buffer_size);
}
