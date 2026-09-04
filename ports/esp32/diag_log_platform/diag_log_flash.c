#include "diag_log_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "denzic_diag_log_store.h"

#include "esp_log.h"
#include "esp_attr.h"
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
/* Keep enough post-reset history to cover a normal wake/capsule interaction
 * even while the status renderer emits frame telemetry.  At 256 events the
 * RTC ring retains roughly 20-30 seconds on the production cadence, while
 * remaining small enough for RTC_NOINIT on ESP32-S3. */
#define DIAG_LOG_RECENT_CAPACITY 256U
#define DIAG_LOG_RECENT_MAGIC UINT32_C(0x44524732)
#define DIAG_LOG_FLASH_SECTOR_EVENT_RESERVE 170U
#define DIAG_LOG_FLASH_MIN_SEVERITY 1U

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_write_queue;
static denzic_diag_log_store_t s_store;
static bool s_store_ready;
static bool s_input_debug_enabled;
static portMUX_TYPE s_input_debug_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_recent_lock = portMUX_INITIALIZER_UNLOCKED;
RTC_NOINIT_ATTR static uint32_t s_recent_magic;
RTC_NOINIT_ATTR static uint16_t s_recent_write_index;
RTC_NOINIT_ATTR static uint16_t s_recent_count;
RTC_NOINIT_ATTR static denzic_diag_log_event_t
    s_recent_events[DIAG_LOG_RECENT_CAPACITY];
/* Writer touches flash; stack must stay internal DRAM (static BSS), never xTaskCreate
 * under SPIRAM_USE_MALLOC (large/fragmented heaps fall into PSRAM). */
static StaticTask_t s_diag_log_writer_tcb;
static StackType_t s_diag_log_writer_stack[DIAG_LOG_WRITER_STACK_SIZE];

static void diag_log_platform_writer_task(void *arg);
static const char *diag_port_source_name(void *context, uint16_t source);

static void diag_log_recent_init(void)
{
    portENTER_CRITICAL(&s_recent_lock);
    if (s_recent_magic != DIAG_LOG_RECENT_MAGIC ||
        s_recent_write_index >= DIAG_LOG_RECENT_CAPACITY ||
        s_recent_count > DIAG_LOG_RECENT_CAPACITY) {
        s_recent_write_index = 0U;
        s_recent_count = 0U;
        memset(s_recent_events, 0, sizeof(s_recent_events));
        s_recent_magic = DIAG_LOG_RECENT_MAGIC;
    }
    portEXIT_CRITICAL(&s_recent_lock);
}

static void diag_log_recent_write(const denzic_diag_log_event_t *event)
{
    if (event == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_recent_lock);
    s_recent_events[s_recent_write_index] = *event;
    s_recent_write_index =
        (uint16_t)((s_recent_write_index + 1U) % DIAG_LOG_RECENT_CAPACITY);
    if (s_recent_count < DIAG_LOG_RECENT_CAPACITY) {
        s_recent_count++;
    }
    portEXIT_CRITICAL(&s_recent_lock);
}

static uint32_t diag_log_recent_dump_last(uint32_t count, uint16_t source, bool use_source_filter)
{
    if (count == 0U) {
        return 0U;
    }
    if (count > DIAG_LOG_RECENT_CAPACITY) {
        count = DIAG_LOG_RECENT_CAPACITY;
    }

    denzic_diag_log_event_t *snapshot =
        (denzic_diag_log_event_t *)malloc(DIAG_LOG_RECENT_CAPACITY * sizeof(*snapshot));
    if (snapshot == NULL) {
        return 0U;
    }

    uint32_t available = 0U;
    portENTER_CRITICAL(&s_recent_lock);
    available = s_recent_count;
    if (available > DIAG_LOG_RECENT_CAPACITY) {
        available = DIAG_LOG_RECENT_CAPACITY;
    }
    uint32_t take = available;
    uint16_t start = (uint16_t)(
        (s_recent_write_index + DIAG_LOG_RECENT_CAPACITY - available) %
        DIAG_LOG_RECENT_CAPACITY);
    for (uint32_t i = 0U; i < available; ++i) {
        snapshot[i] = s_recent_events[(start + i) % DIAG_LOG_RECENT_CAPACITY];
    }
    portEXIT_CRITICAL(&s_recent_lock);

    denzic_diag_log_event_t *matches = NULL;
    uint32_t match_count = 0U;
    if (use_source_filter) {
        matches = (denzic_diag_log_event_t *)malloc(count * sizeof(*matches));
        if (matches == NULL) {
            free(snapshot);
            return 0U;
        }
        for (uint32_t i = 0U; i < take; ++i) {
            if (snapshot[i].source != source) {
                continue;
            }
            if (match_count < count) {
                matches[match_count++] = snapshot[i];
            } else {
                memmove(matches, matches + 1U, (count - 1U) * sizeof(*matches));
                matches[count - 1U] = snapshot[i];
            }
        }
    }

    const denzic_diag_log_event_t *events = use_source_filter ? matches : snapshot;
    uint32_t event_count = use_source_filter
        ? match_count
        : (take < count ? take : count);
    uint32_t event_start = use_source_filter ? 0U : (take - event_count);
    uint32_t dumped = 0U;
    for (uint32_t i = 0U; i < event_count; ++i) {
        const denzic_diag_log_event_t *event = &events[event_start + i];
        const char *source_name = diag_port_source_name(NULL, event->source);
        printf(
            "{\"t\":%" PRIu32 ",\"src\":\"%s\",\"evt\":%u,\"sev\":\"%s\",\"a1\":%" PRIu32 ",\"a2\":%" PRIu32 ",\"a3\":%" PRIu32 ",\"a4\":%" PRIu32 "}\n",
            event->timestamp_ms,
            source_name != NULL ? source_name : "unknown",
            (unsigned)event->event,
            event->severity >= 2U
                ? "ERROR"
                : (event->severity >= 1U ? "WARN" : "INFO"),
            event->arg1,
            event->arg2,
            event->arg3,
            event->arg4);
        dumped++;
    }
    fflush(stdout);
    free(matches);
    free(snapshot);
    return dumped;
}

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
    diag_log_recent_init();
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

    /* The recent ring is the live diagnostic source and survives watchdog
     * resets. Flash is an archive, not a synchronous event sink: continuously
     * persisting INFO traffic filled this 1 MiB partition in a few hours, then
     * sector rotation erased flash while BLE + I2S/GDMA were active and the
     * ESP32-S3 entered INT_WDT. Keep one whole sector in reserve so the runtime
     * writer never has to erase, and archive only warnings/errors. */
    diag_log_recent_write(&evt);
    if (severity < DIAG_LOG_FLASH_MIN_SEVERITY ||
        s_store.retained_events >=
            s_store.capacity_events - DIAG_LOG_FLASH_SECTOR_EVENT_RESERVE) {
        return;
    }

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
    portENTER_CRITICAL(&s_recent_lock);
    uint32_t count = s_recent_count;
    portEXIT_CRITICAL(&s_recent_lock);
    return count;
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
    if (diag_log_recent_dump_last(count, 0U, false) == 0U) {
        denzic_diag_log_store_dump_last(&s_store, count);
    }
}

void diag_log_platform_dump_last_by_source(uint32_t count, uint16_t source)
{
    if (!s_store_ready) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }
    if (diag_log_recent_dump_last(count, source, true) == 0U) {
        denzic_diag_log_store_dump_last_by_source(&s_store, count, source);
    }
}

void diag_log_platform_clear(void)
{
    portENTER_CRITICAL(&s_recent_lock);
    s_recent_write_index = 0U;
    s_recent_count = 0U;
    memset(s_recent_events, 0, sizeof(s_recent_events));
    s_recent_magic = DIAG_LOG_RECENT_MAGIC;
    portEXIT_CRITICAL(&s_recent_lock);
}

bool diag_log_platform_is_dumping(void)
{
    return denzic_diag_log_store_is_dumping(&s_store);
}

uint32_t diag_log_platform_read_range(uint32_t offset, uint32_t limit,
                                       void *buffer, uint32_t buffer_size)
{
    if (buffer == NULL || limit == 0U ||
        buffer_size < DENZIC_DIAG_LOG_EVENT_WIRE_BYTES) {
        return 0U;
    }

    uint32_t max_events = buffer_size / DENZIC_DIAG_LOG_EVENT_WIRE_BYTES;
    if (limit > max_events) {
        limit = max_events;
    }

    portENTER_CRITICAL(&s_recent_lock);
    uint32_t count = s_recent_count;
    if (offset >= count) {
        portEXIT_CRITICAL(&s_recent_lock);
        return 0U;
    }
    if (limit > count - offset) {
        limit = count - offset;
    }
    uint16_t oldest = (uint16_t)(
        (s_recent_write_index + DIAG_LOG_RECENT_CAPACITY - s_recent_count) %
        DIAG_LOG_RECENT_CAPACITY);
    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t i = 0U; i < limit; ++i) {
        uint16_t slot =
            (uint16_t)((oldest + offset + i) % DIAG_LOG_RECENT_CAPACITY);
        memcpy(
            out + i * DENZIC_DIAG_LOG_EVENT_WIRE_BYTES,
            &s_recent_events[slot],
            DENZIC_DIAG_LOG_EVENT_WIRE_BYTES);
    }
    portEXIT_CRITICAL(&s_recent_lock);
    return limit;
}
