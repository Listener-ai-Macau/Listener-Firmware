#include "diag_log_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
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
static bool s_initialized;

/* USB command parser */
#define DIAG_USB_PREFIX '~'
#define DIAG_USB_CMD_PREFIX "DIAGLOG:"
#define DIAG_USB_CMD_MAX 32

static bool s_usb_active;
static bool s_usb_prefix_matched;
static size_t s_usb_length;
static char s_usb_buffer[DIAG_USB_CMD_MAX];

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

        s_retained_events += header.count;

        if (header.sequence > best_seq || best_seq == 0) {
            best_seq = header.sequence;
            best_sector = (uint16_t)i;
            best_count = header.count;
        }
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
    s_retained_events++;

    diag_sector_header_t header;
    read_sector_header(s_write_sector, &header);
    header.count = s_write_offset;
    write_sector_header(s_write_sector, &header);

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

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Find minimum sequence to start iteration in chronological order */
    uint16_t min_seq = UINT16_MAX;
    uint16_t start_sec = 0;
    for (uint32_t sec = 0; sec < s_total_sectors; sec++) {
        diag_sector_header_t header;
        if (read_sector_header((uint16_t)sec, &header) == ESP_OK
            && header.magic == DIAG_LOG_MAGIC && header.count > 0) {
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
        if (ret != ESP_OK || header.magic != DIAG_LOG_MAGIC || header.count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < header.count; idx++) {
            diag_event_t evt;
            ret = read_event(sec, idx, &evt);
            if (ret != ESP_OK) continue;
            dump_event_json(&evt);
            dumped++;
        }
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "DIAGLOG DUMP: %" PRIu32 " events", dumped);
}

void diag_log_platform_dump_last(uint32_t count)
{
    if (!s_initialized || s_partition == NULL) {
        ESP_LOGI(TAG, "DIAGLOG: not initialized");
        return;
    }

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
            && header.magic == DIAG_LOG_MAGIC) {
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
        if (ret != ESP_OK || header.magic != DIAG_LOG_MAGIC || header.count == 0) {
            continue;
        }

        for (uint16_t idx = 0; idx < header.count; idx++) {
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

/*
 * USB command parser: ~DIAGLOG:DUMP / ~DIAGLOG:COUNT / ~DIAGLOG:CLEAR / ~DIAGLOG:LAST:N
 *
 * Only consumes bytes that are part of a recognized ~DIAGLOG: command.
 * Non-DIAGLOG ~ commands (e.g. ~VREC:TOGGLE) are NOT consumed — the parser
 * resets and returns false so the caller can pass the bytes to the next handler.
 */
bool diag_log_consume_usb_command(uint8_t input_char)
{
    if (!s_usb_active) {
        if (input_char == (uint8_t)DIAG_USB_PREFIX) {
            s_usb_active = true;
            s_usb_prefix_matched = false;
            s_usb_length = 0;
            memset(s_usb_buffer, 0, sizeof(s_usb_buffer));
            return true; /* consumed the ~ */
        }
        return false;
    }

    /* Still checking if this is DIAGLOG: prefix */
    if (!s_usb_prefix_matched) {
        if (input_char == '\r') {
            return true; /* consume CR inside potential command */
        }

        if (input_char == '\n') {
            /* End of command before DIAGLOG: prefix matched */
            s_usb_active = false;
            /* This was not a DIAGLOG command, but we already consumed the ~ */
            return true;
        }

        /* Build up prefix to check against DIAGLOG: */
        if (s_usb_length < strlen(DIAG_USB_CMD_PREFIX)) {
            s_usb_buffer[s_usb_length++] = (char)input_char;
            s_usb_buffer[s_usb_length] = '\0';

            /* Check if still matches DIAGLOG: prefix so far */
            if (strncmp(s_usb_buffer, DIAG_USB_CMD_PREFIX, s_usb_length) != 0) {
                /* Not a DIAGLOG command — reset and return consumed (we ate the ~) */
                s_usb_active = false;
                return true;
            }

            /* Full prefix matched */
            if (s_usb_length == strlen(DIAG_USB_CMD_PREFIX)) {
                s_usb_prefix_matched = true;
                s_usb_length = 0;
                memset(s_usb_buffer, 0, sizeof(s_usb_buffer));
            }
            return true;
        }

        /* Shouldn't reach here */
        s_usb_active = false;
        return true;
    }

    /* Prefix matched, now collecting the command */
    if (input_char == '\r') {
        return true;
    }

    if (input_char == '\n') {
        s_usb_active = false;
        s_usb_buffer[s_usb_length] = '\0';

        const char *cmd = s_usb_buffer;

        if (strcmp(cmd, "DUMP") == 0) {
            diag_log_platform_dump();
            return true;
        }
        if (strcmp(cmd, "COUNT") == 0) {
            ESP_LOGI(TAG, "DIAGLOG COUNT: %" PRIu32, diag_log_platform_count());
            return true;
        }
        if (strcmp(cmd, "CLEAR") == 0) {
            diag_log_platform_clear();
            return true;
        }
        if (strncmp(cmd, "LAST:", 5) == 0) {
            uint32_t n = (uint32_t)atoi(cmd + 5);
            if (n > 0) {
                diag_log_platform_dump_last(n);
            }
            return true;
        }

        ESP_LOGW(TAG, "DIAGLOG: unknown command: %s", cmd);
        return true;
    }

    if (s_usb_length + 1 >= sizeof(s_usb_buffer)) {
        s_usb_active = false;
        return true;
    }

    s_usb_buffer[s_usb_length++] = (char)input_char;
    return true;
}
