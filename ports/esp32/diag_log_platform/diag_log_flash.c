#include "diag_log_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "spi_flash_mmap.h"
#include "watchdog_platform.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "diag_log";

#define DIAG_LOG_SECTOR_SIZE 4096U
#define DIAG_LOG_MAGIC       0xD1A90001U
#define DIAG_LOG_DUMP_PACE_EVENTS 64U
#define DIAG_LOG_SNAPSHOT_READ_BATCH_EVENTS 32U

typedef struct {
    uint32_t magic;
    uint16_t sequence;
    uint16_t count;
    uint32_t first_timestamp;
    uint32_t reserved;
} diag_sector_header_t;

#define DIAG_SECTOR_HEADER_SIZE 16U

typedef struct {
    uint32_t timestamp_ms;
    uint16_t source;
    uint8_t  event;
    uint8_t  severity;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
} diag_event_t;

typedef struct {
    uint16_t sector;
    uint16_t index;
    uint16_t sequence;
} diag_read_slot_t;

#define DIAG_EVENT_SIZE sizeof(diag_event_t)
_Static_assert(DIAG_EVENT_SIZE == DIAG_LOG_EVENT_WIRE_BYTES,
               "diag_event_t wire size must stay stable for BLE diagnostic export");
#define DIAG_EVENTS_PER_SECTOR ((DIAG_LOG_SECTOR_SIZE - DIAG_SECTOR_HEADER_SIZE) / DIAG_EVENT_SIZE)

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static uint16_t *s_sector_counts;
static uint16_t *s_sector_sequences;
static uint32_t s_total_sectors;
static uint16_t s_write_sector;
static uint16_t s_write_offset;
static uint16_t s_sector_sequence;
static uint32_t s_retained_events;
static uint32_t s_capacity_events;
static bool s_initialized;
static bool s_dumping;
static portMUX_TYPE s_dumping_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_input_debug_enabled;
static portMUX_TYPE s_input_debug_lock = portMUX_INITIALIZER_UNLOCKED;

static void diag_log_platform_set_dumping(bool dumping)
{
    portENTER_CRITICAL(&s_dumping_lock);
    s_dumping = dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
}

static bool diag_log_platform_get_dumping(void)
{
    portENTER_CRITICAL(&s_dumping_lock);
    bool dumping = s_dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
    return dumping;
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

static uint32_t timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static esp_err_t read_sector_header(uint16_t sector, diag_sector_header_t *header)
{
    size_t offset = (size_t)sector * DIAG_LOG_SECTOR_SIZE;
    return esp_partition_read(s_partition, offset, header, sizeof(diag_sector_header_t));
}

static esp_err_t write_sector_header(uint16_t sector, const diag_sector_header_t *header)
{
    size_t offset = (size_t)sector * DIAG_LOG_SECTOR_SIZE;
    return esp_partition_write(s_partition, offset, header, sizeof(diag_sector_header_t));
}

static esp_err_t erase_sector(uint16_t sector)
{
    size_t offset = (size_t)sector * DIAG_LOG_SECTOR_SIZE;
    return esp_partition_erase_range(s_partition, offset, DIAG_LOG_SECTOR_SIZE);
}

static esp_err_t read_event(uint16_t sector, uint16_t index, diag_event_t *evt)
{
    size_t offset = (size_t)sector * DIAG_LOG_SECTOR_SIZE
                  + DIAG_SECTOR_HEADER_SIZE
                  + (size_t)index * DIAG_EVENT_SIZE;
    return esp_partition_read(s_partition, offset, evt, DIAG_EVENT_SIZE);
}

static bool event_is_erased(const diag_event_t *evt)
{
    const uint8_t *bytes = (const uint8_t *)evt;
    for (size_t i = 0; i < sizeof(*evt); i++) {
        if (bytes[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static uint16_t retained_count_from_sector(uint16_t sector, const diag_sector_header_t *header)
{
    if (header == NULL || header->magic != DIAG_LOG_MAGIC) {
        return 0;
    }

    diag_event_t evt;
    const uint16_t last_index = (uint16_t)(DIAG_EVENTS_PER_SECTOR - 1U);
    if (read_event(sector, last_index, &evt) != ESP_OK) {
        return 0;
    }
    if (!event_is_erased(&evt)) {
        return (uint16_t)DIAG_EVENTS_PER_SECTOR;
    }

    if (read_event(sector, 0, &evt) != ESP_OK || event_is_erased(&evt)) {
        return 0;
    }

    uint16_t low = 1U;
    uint16_t high = last_index;
    while (low < high) {
        uint16_t mid = (uint16_t)(low + ((high - low) / 2U));
        if (read_event(sector, mid, &evt) != ESP_OK || event_is_erased(&evt)) {
            high = mid;
        } else {
            low = (uint16_t)(mid + 1U);
        }
    }
    return low;
}

static uint16_t cached_retained_count_from_sector(uint16_t sector, const diag_sector_header_t *header)
{
    if (header == NULL || header->magic != DIAG_LOG_MAGIC) {
        return 0;
    }
    if (s_sector_counts != NULL && sector < s_total_sectors) {
        return s_sector_counts[sector];
    }
    return retained_count_from_sector(sector, header);
}

static void pace_dump_output(uint32_t event_counter)
{
    if (event_counter == 0) {
        return;
    }

    if ((event_counter % DIAG_LOG_DUMP_PACE_EVENTS) != 0) {
        return;
    }

    fflush(stdout);
    watchdog_platform_feed_current_task();
    vTaskDelay(1);
}

static void pace_flash_snapshot(void)
{
    watchdog_platform_feed_current_task();
    vTaskDelay(1);
}

static void find_write_position(void)
{
    uint16_t best_seq = 0;
    uint16_t best_sector = 0;
    uint16_t best_count = 0;

    s_retained_events = 0;
    if (s_sector_counts != NULL) {
        memset(s_sector_counts, 0, s_total_sectors * sizeof(s_sector_counts[0]));
    }
    if (s_sector_sequences != NULL) {
        memset(s_sector_sequences, 0, s_total_sectors * sizeof(s_sector_sequences[0]));
    }

    for (uint32_t i = 0; i < s_total_sectors; i++) {
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header((uint16_t)i, &header);
        if (ret != ESP_OK || header.magic != DIAG_LOG_MAGIC) {
            continue;
        }

        uint16_t count = retained_count_from_sector((uint16_t)i, &header);
        s_retained_events += count;
        if (s_sector_counts != NULL) {
            s_sector_counts[i] = count;
        }
        if (s_sector_sequences != NULL) {
            s_sector_sequences[i] = header.sequence;
        }

        if (header.sequence > best_seq || best_seq == 0) {
            best_seq = header.sequence;
            best_sector = (uint16_t)i;
            best_count = count;
        }
    }

    if (s_retained_events > s_capacity_events) {
        s_retained_events = s_capacity_events;
    }

    if (best_seq == 0) {
        s_write_sector = 0;
        s_write_offset = 0;
        s_sector_sequence = 1;
        return;
    }

    s_sector_sequence = best_seq + 1;
    s_write_sector = best_sector;
    s_write_offset = best_count;

    if (s_write_offset >= DIAG_EVENTS_PER_SECTOR) {
        s_write_sector = (best_sector + 1) % (uint16_t)s_total_sectors;
        s_write_offset = 0;
    }
}

static const char *source_name(uint16_t src)
{
    switch (src) {
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

static const char *severity_name(uint8_t sev)
{
    switch (sev) {
    case 0: return "INFO";
    case 1: return "WARN";
    case 2: return "ERROR";
    default: return "?";
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

    s_total_sectors = s_partition->size / DIAG_LOG_SECTOR_SIZE;
    s_capacity_events = s_total_sectors * DIAG_EVENTS_PER_SECTOR;
    ESP_LOGI(TAG, "diag_log partition: %uKB %u sectors",
             (unsigned)(s_partition->size / 1024), (unsigned)s_total_sectors);

    free(s_sector_counts);
    free(s_sector_sequences);
    s_sector_counts = (uint16_t *)calloc(s_total_sectors, sizeof(uint16_t));
    s_sector_sequences = (uint16_t *)calloc(s_total_sectors, sizeof(uint16_t));
    if (s_sector_counts == NULL || s_sector_sequences == NULL) {
        ESP_LOGE(TAG, "sector metadata allocation failed");
        free(s_sector_counts);
        free(s_sector_sequences);
        s_sector_counts = NULL;
        s_sector_sequences = NULL;
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        free(s_sector_counts);
        free(s_sector_sequences);
        s_sector_counts = NULL;
        s_sector_sequences = NULL;
        return;
    }

    find_write_position();
    s_initialized = true;
    ESP_LOGI(TAG, "diag_log ready: write_sector=%u write_offset=%u retained=%" PRIu32,
             (unsigned)s_write_sector, (unsigned)s_write_offset, s_retained_events);
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
    if (!s_initialized || s_partition == NULL) {
        return;
    }

    diag_event_t evt = {
        .timestamp_ms = timestamp_ms(),
        .source = source,
        .event = event,
        .severity = severity,
        .arg1 = arg1,
        .arg2 = arg2,
        .arg3 = arg3,
        .arg4 = arg4,
    };

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_write_offset == 0) {
        diag_sector_header_t old_header;
        if (read_sector_header(s_write_sector, &old_header) == ESP_OK) {
            uint16_t old_count =
                s_sector_counts != NULL
                    ? s_sector_counts[s_write_sector]
                    : retained_count_from_sector(s_write_sector, &old_header);
            s_retained_events = old_count > s_retained_events ? 0 : s_retained_events - old_count;
        }

        esp_err_t ret = erase_sector(s_write_sector);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "erase sector %u failed: %s", (unsigned)s_write_sector, esp_err_to_name(ret));
            xSemaphoreGive(s_mutex);
            return;
        }
        if (s_sector_counts != NULL) {
            s_sector_counts[s_write_sector] = 0;
        }
        if (s_sector_sequences != NULL) {
            s_sector_sequences[s_write_sector] = 0;
        }

        diag_sector_header_t header = {
            .magic = DIAG_LOG_MAGIC,
            .sequence = s_sector_sequence++,
            .count = 0,
            .first_timestamp = evt.timestamp_ms,
            .reserved = 0,
        };
        ret = write_sector_header(s_write_sector, &header);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return;
        }
        if (s_sector_sequences != NULL) {
            s_sector_sequences[s_write_sector] = header.sequence;
        }
    }

    uint16_t written_sector = s_write_sector;
    size_t offset = (size_t)s_write_sector * DIAG_LOG_SECTOR_SIZE
                  + DIAG_SECTOR_HEADER_SIZE
                  + (size_t)s_write_offset * DIAG_EVENT_SIZE;
    esp_err_t ret = esp_partition_write(s_partition, offset, &evt, DIAG_EVENT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "write event failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return;
    }

    s_write_offset++;
    if (s_sector_counts != NULL && s_sector_counts[written_sector] < s_write_offset) {
        s_sector_counts[written_sector] = s_write_offset;
    }
    if (s_retained_events < s_capacity_events) {
        s_retained_events++;
    }

    if (s_write_offset >= DIAG_EVENTS_PER_SECTOR) {
        s_write_sector = (s_write_sector + 1) % (uint16_t)s_total_sectors;
        s_write_offset = 0;
    }

    xSemaphoreGive(s_mutex);
}

uint32_t diag_log_platform_count(void)
{
    if (!s_initialized) {
        return 0;
    }
    return s_retained_events;
}

/* Find the sector with the minimum sequence for ordered iteration */
static void dump_event_json(const diag_event_t *evt)
{
    printf("{\"t\":%" PRIu32 ",\"src\":\"%s\",\"evt\":%u,\"sev\":\"%s\","
           "\"a1\":%" PRIu32 ",\"a2\":%" PRIu32 ",\"a3\":%" PRIu32 ",\"a4\":%" PRIu32 "}\n",
           evt->timestamp_ms, source_name(evt->source), (unsigned)evt->event,
           severity_name(evt->severity),
           evt->arg1, evt->arg2, evt->arg3, evt->arg4);
}

static bool find_oldest_sector(uint16_t *out_start_sector)
{
    if (out_start_sector == NULL) {
        return false;
    }

    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    bool found = false;

    for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
        diag_sector_header_t header;
        uint16_t count = 0;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (read_sector_header((uint16_t)sec, &header) == ESP_OK) {
            count = cached_retained_count_from_sector((uint16_t)sec, &header);
        }
        xSemaphoreGive(s_mutex);

        if (count > 0 && (!found || header.sequence < min_seq)) {
            min_seq = header.sequence;
            start_sec = (uint16_t)sec;
            found = true;
        }
    }

    *out_start_sector = start_sec;
    return found;
}

static uint16_t snapshot_sector_events(uint16_t sector, diag_event_t *events, uint16_t max_events)
{
    if (events == NULL || max_events == 0) {
        return 0;
    }

    uint16_t count = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    diag_sector_header_t header;
    if (read_sector_header(sector, &header) == ESP_OK) {
        count = cached_retained_count_from_sector(sector, &header);
        if (count > max_events) {
            count = max_events;
        }
    }
    xSemaphoreGive(s_mutex);

    uint16_t read_count = 0;
    while (read_count < count) {
        uint16_t batch_end = (uint16_t)(read_count + DIAG_LOG_SNAPSHOT_READ_BATCH_EVENTS);
        if (batch_end > count) {
            batch_end = count;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        esp_err_t ret = ESP_OK;
        for (uint16_t idx = read_count; idx < batch_end; idx++) {
            ret = read_event(sector, idx, &events[idx]);
            if (ret != ESP_OK) {
                break;
            }
        }
        xSemaphoreGive(s_mutex);
        if (ret != ESP_OK) {
            return read_count;
        }

        read_count = batch_end;
        if (read_count < count) {
            pace_flash_snapshot();
        }
    }

    return read_count;
}

static uint32_t capped_last_count(uint32_t count)
{
    uint32_t capacity = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    capacity = s_capacity_events;
    xSemaphoreGive(s_mutex);

    if (capacity > 0 && count > capacity) {
        return capacity;
    }
    return count;
}

void diag_log_platform_dump(void)
{
    if (!s_initialized || s_partition == NULL) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }

    diag_log_platform_set_dumping(true);
    uint16_t start_sec = 0;
    if (!find_oldest_sector(&start_sec)) {
        diag_log_platform_set_dumping(false);
        ESP_LOGI(TAG, "DIAGLOG DUMP: 0 events");
        return;
    }

    diag_event_t *sector_events = (diag_event_t *)malloc(DIAG_EVENTS_PER_SECTOR * sizeof(diag_event_t));
    if (sector_events == NULL) {
        diag_log_platform_set_dumping(false);
        ESP_LOGW(TAG, "DIAGLOG DUMP: sector snapshot allocation failed");
        return;
    }

    uint32_t dumped = 0;
    for (uint32_t i = 0; i < s_total_sectors; i++) {
        uint16_t sec = (start_sec + (uint16_t)i) % (uint16_t)s_total_sectors;
        uint16_t count = snapshot_sector_events(sec, sector_events, DIAG_EVENTS_PER_SECTOR);
        if (count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < count; idx++) {
            dump_event_json(&sector_events[idx]);
            dumped++;
            pace_dump_output(dumped);
        }
    }

    fflush(stdout);
    watchdog_platform_feed_current_task();
    free(sector_events);
    diag_log_platform_set_dumping(false);
    ESP_LOGI(TAG, "DIAGLOG DUMP: %" PRIu32 " events", dumped);
}

static void diag_log_platform_dump_last_filtered(uint32_t count, uint16_t source, bool use_source_filter)
{
    if (!s_initialized || s_partition == NULL) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }

    diag_log_platform_set_dumping(true);
    uint16_t start_sec = 0;
    if (!find_oldest_sector(&start_sec)) {
        diag_log_platform_set_dumping(false);
        ESP_LOGI(TAG, "DIAGLOG LAST %" PRIu32 ": dumped 0 events", count);
        return;
    }

    uint32_t limit = capped_last_count(count);
    if (limit == 0) {
        diag_log_platform_set_dumping(false);
        ESP_LOGI(TAG, "DIAGLOG LAST %" PRIu32 ": dumped 0 events", count);
        return;
    }

    diag_event_t *sector_events = (diag_event_t *)malloc(DIAG_EVENTS_PER_SECTOR * sizeof(diag_event_t));
    diag_event_t *matches = (diag_event_t *)malloc(limit * sizeof(diag_event_t));
    if (sector_events == NULL || matches == NULL) {
        free(sector_events);
        free(matches);
        diag_log_platform_set_dumping(false);
        ESP_LOGW(TAG, "DIAGLOG LAST %" PRIu32 ": snapshot allocation failed", count);
        return;
    }

    uint32_t matched = 0;
    uint32_t kept = 0;
    uint32_t scanned = 0;
    for (uint32_t offset = s_total_sectors; offset > 0; offset--) {
        uint16_t sec = (start_sec + (uint16_t)(offset - 1)) % (uint16_t)s_total_sectors;
        uint16_t sector_count = snapshot_sector_events(sec, sector_events, DIAG_EVENTS_PER_SECTOR);
        if (sector_count == 0) {
            continue;
        }

        for (uint16_t idx = sector_count; idx > 0; idx--) {
            diag_event_t *evt = &sector_events[idx - 1];
            scanned++;
            pace_dump_output(scanned);
            if (use_source_filter && evt->source != source) {
                continue;
            }
            matched++;
            if (kept < limit) {
                matches[kept++] = *evt;
            }
        }
    }

    uint32_t dumped = 0;
    for (uint32_t idx = kept; idx > 0; idx--) {
        dump_event_json(&matches[idx - 1]);
        dumped++;
        pace_dump_output(dumped);
    }

    free(matches);
    free(sector_events);
    fflush(stdout);
    watchdog_platform_feed_current_task();
    diag_log_platform_set_dumping(false);
    if (use_source_filter) {
        ESP_LOGI(TAG, "DIAGLOG LAST %" PRIu32 " source=%s: dumped %" PRIu32 " of %" PRIu32 " matching events",
                 count, source_name(source), dumped, matched);
    } else {
        ESP_LOGI(TAG, "DIAGLOG LAST %" PRIu32 ": dumped %" PRIu32 " events", count, dumped);
    }
}
void diag_log_platform_dump_last(uint32_t count)
{
    diag_log_platform_dump_last_filtered(count, 0, false);
}

void diag_log_platform_dump_last_by_source(uint32_t count, uint16_t source)
{
    diag_log_platform_dump_last_filtered(count, source, true);
}

void diag_log_platform_clear(void)
{
    if (!s_initialized || s_partition == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (uint32_t i = 0; i < s_total_sectors; i++) {
        erase_sector((uint16_t)i);
    }

    s_write_sector = 0;
    s_write_offset = 0;
    s_sector_sequence = 1;
    s_retained_events = 0;
    if (s_sector_counts != NULL) {
        memset(s_sector_counts, 0, s_total_sectors * sizeof(s_sector_counts[0]));
    }
    if (s_sector_sequences != NULL) {
        memset(s_sector_sequences, 0, s_total_sectors * sizeof(s_sector_sequences[0]));
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "DIAGLOG CLEAR: all logs erased");
}

bool diag_log_platform_is_dumping(void)
{
    return diag_log_platform_get_dumping();
}

uint32_t diag_log_platform_read_range(uint32_t offset, uint32_t limit,
                                       void *buffer, uint32_t buffer_size)
{
    if (!s_initialized || s_partition == NULL || buffer == NULL) {
        return 0;
    }

    if (limit == 0 || buffer_size < DIAG_EVENT_SIZE) {
        return 0;
    }

    uint32_t max_events = buffer_size / DIAG_EVENT_SIZE;
    if (limit > max_events) {
        limit = max_events;
    }

    diag_read_slot_t *slots = (diag_read_slot_t *)calloc(limit, sizeof(diag_read_slot_t));
    if (slots == NULL) {
        return 0;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Find minimum sequence to start iteration in chronological order. */
    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    bool have_start = false;
    if (s_sector_counts != NULL && s_sector_sequences != NULL) {
        for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
            if (s_sector_counts[sec] == 0) {
                continue;
            }
            if (!have_start || s_sector_sequences[sec] < min_seq) {
                min_seq = s_sector_sequences[sec];
                start_sec = (uint16_t)sec;
                have_start = true;
            }
        }
    }

    uint32_t global_idx = 0;
    uint32_t selected = 0;

    for (uint32_t i = 0; have_start && i < s_total_sectors && selected < limit; i++) {
        uint16_t sec = (start_sec + (uint16_t)i) % (uint16_t)s_total_sectors;
        uint16_t sector_count = s_sector_counts[sec];
        if (sector_count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < sector_count && selected < limit; idx++, global_idx++) {
            if (global_idx < offset) {
                continue;
            }

            slots[selected].sector = sec;
            slots[selected].index = idx;
            slots[selected].sequence = s_sector_sequences[sec];
            selected++;
        }
    }

    xSemaphoreGive(s_mutex);

    uint32_t written = 0;
    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t i = 0; i < selected; i++) {
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header(slots[i].sector, &header);
        if (ret != ESP_OK || header.magic != DIAG_LOG_MAGIC || header.sequence != slots[i].sequence) {
            continue;
        }

        diag_event_t evt;
        ret = read_event(slots[i].sector, slots[i].index, &evt);
        if (ret != ESP_OK || event_is_erased(&evt)) {
            continue;
        }

        memcpy(out + written * DIAG_EVENT_SIZE, &evt, DIAG_EVENT_SIZE);
        written++;
    }

    free(slots);
    return written;
}
