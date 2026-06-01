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

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "diag_log";

#define DIAG_LOG_SECTOR_SIZE 4096U
#define DIAG_LOG_MAGIC       0xD1A90001U

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

#define DIAG_EVENT_SIZE sizeof(diag_event_t)
#define DIAG_EVENTS_PER_SECTOR ((DIAG_LOG_SECTOR_SIZE - DIAG_SECTOR_HEADER_SIZE) / DIAG_EVENT_SIZE)

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_mutex;
static uint32_t s_total_sectors;
static uint16_t s_write_sector;
static uint16_t s_write_offset;
static uint16_t s_sector_sequence;
static uint32_t s_retained_events;
static uint32_t s_capacity_events;
static bool s_initialized;
static bool s_dumping;

#define DIAG_USB_CMD_PREFIX "DIAGLOG:"
#define DIAG_USB_CMD_MAX 32

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

    uint16_t count = 0;
    for (uint16_t index = 0; index < DIAG_EVENTS_PER_SECTOR; index++) {
        diag_event_t evt;
        if (read_event(sector, index, &evt) != ESP_OK || event_is_erased(&evt)) {
            break;
        }
        count++;
    }
    return count;
}

static void find_write_position(void)
{
    uint16_t best_seq = 0;
    uint16_t best_sector = 0;
    uint16_t best_count = 0;

    s_retained_events = 0;

    for (uint32_t i = 0; i < s_total_sectors; i++) {
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header((uint16_t)i, &header);
        if (ret != ESP_OK || header.magic != DIAG_LOG_MAGIC) {
            continue;
        }

        uint16_t count = retained_count_from_sector((uint16_t)i, &header);
        s_retained_events += count;

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

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
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
            uint16_t old_count = retained_count_from_sector(s_write_sector, &old_header);
            s_retained_events = old_count > s_retained_events ? 0 : s_retained_events - old_count;
        }

        esp_err_t ret = erase_sector(s_write_sector);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "erase sector %u failed: %s", (unsigned)s_write_sector, esp_err_to_name(ret));
            xSemaphoreGive(s_mutex);
            return;
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
    }

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

void diag_log_platform_dump(void)
{
    if (!s_initialized || s_partition == NULL) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }

    s_dumping = true;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Find minimum sequence to start iteration in chronological order */
    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
        diag_sector_header_t header;
        if (read_sector_header((uint16_t)sec, &header) == ESP_OK
            && header.magic == DIAG_LOG_MAGIC && retained_count_from_sector((uint16_t)sec, &header) > 0) {
            if (header.sequence < min_seq) {
                min_seq = header.sequence;
                start_sec = (uint16_t)sec;
            }
        }
    }

    uint32_t dumped = 0;
    for (uint32_t i = 0; i < s_total_sectors; i++) {
        uint16_t sec = (start_sec + (uint16_t)i) % (uint16_t)s_total_sectors;
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header(sec, &header);
        if (ret != ESP_OK) {
            continue;
        }
        uint16_t count = retained_count_from_sector(sec, &header);
        if (count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < count; idx++) {
            diag_event_t evt;
            ret = read_event(sec, idx, &evt);
            if (ret != ESP_OK) continue;
            dump_event_json(&evt);
            dumped++;
        }
    }

    xSemaphoreGive(s_mutex);
    s_dumping = false;
    ESP_LOGI(TAG, "DIAGLOG DUMP: %" PRIu32 " events", dumped);
}

void diag_log_platform_dump_last(uint32_t count)
{
    if (!s_initialized || s_partition == NULL) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }

    s_dumping = true;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Count total retained events */
    uint32_t total = s_retained_events;
    if (count > total) {
        count = total;
    }

    uint32_t skip = total - count;

    /* Find start sector by minimum sequence */
    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
        diag_sector_header_t header;
        if (read_sector_header((uint16_t)sec, &header) == ESP_OK
            && header.magic == DIAG_LOG_MAGIC && retained_count_from_sector((uint16_t)sec, &header) > 0) {
            if (header.sequence < min_seq) {
                min_seq = header.sequence;
                start_sec = (uint16_t)sec;
            }
        }
    }

    uint32_t skipped = 0;
    uint32_t dumped = 0;
    for (uint32_t i = 0; i < s_total_sectors; i++) {
        uint16_t sec = (start_sec + (uint16_t)i) % (uint16_t)s_total_sectors;
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header(sec, &header);
        if (ret != ESP_OK) {
            continue;
        }
        uint16_t sector_count = retained_count_from_sector(sec, &header);
        if (sector_count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < sector_count; idx++) {
            if (skipped < skip) {
                skipped++;
                continue;
            }

            diag_event_t evt;
            ret = read_event(sec, idx, &evt);
            if (ret != ESP_OK) continue;
            dump_event_json(&evt);
            dumped++;
        }
    }

    xSemaphoreGive(s_mutex);
    s_dumping = false;
    ESP_LOGI(TAG, "DIAGLOG LAST %" PRIu32 ": dumped %" PRIu32 " events", count, dumped);
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

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "DIAGLOG CLEAR: all logs erased");
}

bool diag_log_platform_is_dumping(void)
{
    return s_dumping;
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

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Find minimum sequence to start iteration in chronological order */
    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
        diag_sector_header_t header;
        if (read_sector_header((uint16_t)sec, &header) == ESP_OK
            && header.magic == DIAG_LOG_MAGIC
            && retained_count_from_sector((uint16_t)sec, &header) > 0) {
            if (header.sequence < min_seq) {
                min_seq = header.sequence;
                start_sec = (uint16_t)sec;
            }
        }
    }

    uint32_t global_idx = 0;
    uint32_t written = 0;
    uint8_t *out = (uint8_t *)buffer;

    for (uint32_t i = 0; i < s_total_sectors && written < limit; i++) {
        uint16_t sec = (start_sec + (uint16_t)i) % (uint16_t)s_total_sectors;
        diag_sector_header_t header;
        esp_err_t ret = read_sector_header(sec, &header);
        if (ret != ESP_OK) {
            continue;
        }
        uint16_t sector_count = retained_count_from_sector(sec, &header);
        if (sector_count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < sector_count && written < limit; idx++, global_idx++) {
            if (global_idx < offset) {
                continue;
            }

            diag_event_t evt;
            ret = read_event(sec, idx, &evt);
            if (ret != ESP_OK) {
                continue;
            }

            memcpy(out + written * DIAG_EVENT_SIZE, &evt, DIAG_EVENT_SIZE);
            written++;
        }
    }

    xSemaphoreGive(s_mutex);
    return written;
}

bool diag_log_consume_usb_command(const char *line)
{
    if (line == NULL) {
        return false;
    }

    if (*line == '~') {
        line++;
    }

    size_t prefix_len = strlen(DIAG_USB_CMD_PREFIX);
    if (strncmp(line, DIAG_USB_CMD_PREFIX, prefix_len) != 0) {
        return false;
    }

    char cmd_buffer[DIAG_USB_CMD_MAX] = {0};
    const char *cmd_start = line + prefix_len;
    size_t cmd_len = 0;
    while (cmd_start[cmd_len] != '\0' && cmd_start[cmd_len] != '\r' && cmd_start[cmd_len] != '\n') {
        if (cmd_len + 1 >= sizeof(cmd_buffer)) {
            ESP_LOGW(TAG, "DIAGLOG: command too long");
            return true;
        }
        cmd_buffer[cmd_len] = cmd_start[cmd_len];
        cmd_len++;
    }

    if (strcmp(cmd_buffer, "DUMP") == 0) {
        diag_log_platform_dump();
        return true;
    }
    if (strcmp(cmd_buffer, "COUNT") == 0) {
        ESP_LOGI(TAG, "DIAGLOG COUNT: %" PRIu32, diag_log_platform_count());
        return true;
    }
    if (strcmp(cmd_buffer, "CLEAR") == 0) {
        diag_log_platform_clear();
        return true;
    }
    if (strncmp(cmd_buffer, "LAST:", 5) == 0) {
        uint32_t n = (uint32_t)atoi(cmd_buffer + 5);
        if (n > 0) {
            diag_log_platform_dump_last(n);
        }
        return true;
    }

    ESP_LOGW(TAG, "DIAGLOG: unknown command: %s", cmd_buffer);
    return true;
}
