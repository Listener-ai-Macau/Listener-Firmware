#include "diag_log.h"
#include "diag_log_platform.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "diag_log";

static bool s_dumping;
static portMUX_TYPE s_dumping_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_source_mask_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint16_t source;
    const char *name;
    bool info_default_enabled;
} diag_log_source_def_t;

static const diag_log_source_def_t s_source_defs[] = {
    {DIAG_SRC_SYSTEM, "system", true},
    {DIAG_SRC_KEYBOARD, "keyboard", false},
    {DIAG_SRC_BLE_HID, "ble_hid", true},
    {DIAG_SRC_BLE_GAP, "ble_gap", true},
    {DIAG_SRC_AUDIO, "audio", false},
    {DIAG_SRC_VOICE_REC, "voice_rec", false},
    {DIAG_SRC_VOICE_KEY, "voice_key", false},
    {DIAG_SRC_SELF_TEST, "self_test", true},
    {DIAG_SRC_HEALTH, "health", true},
    {DIAG_SRC_BLE_AUDIO, "ble_audio", false},
    {DIAG_SRC_OTA, "ota", true},
    {DIAG_SRC_POWER, "power", true},
    {DIAG_SRC_BOARD, "board", true},
    {DIAG_SRC_STATUS_LED, "status_led", true},
};

static uint32_t s_source_mask;

static uint32_t diag_log_source_bit(uint16_t source)
{
    if (source == 0 || source > 31) {
        return 0;
    }
    return 1u << source;
}

static const diag_log_source_def_t *diag_log_find_source(uint16_t source)
{
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (s_source_defs[i].source == source) {
            return &s_source_defs[i];
        }
    }
    return NULL;
}

static uint32_t diag_log_default_source_mask(void)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (s_source_defs[i].info_default_enabled) {
            mask |= diag_log_source_bit(s_source_defs[i].source);
        }
    }
    return mask;
}

static bool diag_log_source_allowed(uint16_t source, uint8_t severity)
{
    if (severity >= DIAG_SEV_WARN) {
        return true;
    }

    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    bool enabled = (s_source_mask & bit) != 0;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return enabled;
}

#define DIAG_USB_CMD_PREFIX "DIAGLOG:"
#define DIAG_USB_CMD_MAX 64

static void diag_log_set_dumping(bool dumping)
{
    portENTER_CRITICAL(&s_dumping_lock);
    s_dumping = dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
}

static bool diag_log_get_dumping(void)
{
    portENTER_CRITICAL(&s_dumping_lock);
    bool dumping = s_dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
    return dumping;
}

void diag_log_init(void)
{
    portENTER_CRITICAL(&s_source_mask_lock);
    s_source_mask = diag_log_default_source_mask();
    portEXIT_CRITICAL(&s_source_mask_lock);

    diag_log_platform_init();
    diag_log_write(DIAG_SRC_SYSTEM, DIAG_SYS_INIT_RESULT, DIAG_SEV_INFO,
                   DIAG_COMP_DIAG_LOG, 0, 0, 0);
}

void diag_log_write(uint16_t source, uint8_t event, uint8_t severity,
                    uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    if (!diag_log_source_allowed(source, severity)) {
        return;
    }
    diag_log_platform_write(source, event, severity, arg1, arg2, arg3, arg4);
}

uint32_t diag_log_source_mask(void)
{
    portENTER_CRITICAL(&s_source_mask_lock);
    uint32_t mask = s_source_mask;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return mask;
}

bool diag_log_source_is_enabled(uint16_t source)
{
    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    bool enabled = (s_source_mask & bit) != 0;
    portEXIT_CRITICAL(&s_source_mask_lock);
    return enabled;
}

bool diag_log_source_set_enabled(uint16_t source, bool enabled)
{
    uint32_t bit = diag_log_source_bit(source);
    if (bit == 0 || diag_log_find_source(source) == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_source_mask_lock);
    if (enabled) {
        s_source_mask |= bit;
    } else {
        s_source_mask &= ~bit;
    }
    portEXIT_CRITICAL(&s_source_mask_lock);
    return true;
}

const char *diag_log_source_name(uint16_t source)
{
    const diag_log_source_def_t *def = diag_log_find_source(source);
    return def != NULL ? def->name : "unknown";
}

bool diag_log_source_from_name(const char *name, uint16_t *out_source)
{
    if (name == NULL || out_source == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        if (strcmp(name, s_source_defs[i].name) == 0) {
            *out_source = s_source_defs[i].source;
            return true;
        }
    }
    return false;
}

uint32_t diag_log_count(void)
{
    return diag_log_platform_count();
}

void diag_log_dump(void)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump();
    diag_log_set_dumping(false);
}

void diag_log_dump_last(uint32_t count)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump_last(count);
    diag_log_set_dumping(false);
}

void diag_log_dump_last_by_source(uint32_t count, uint16_t source)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump_last_by_source(count, source);
    diag_log_set_dumping(false);
}

void diag_log_clear(void)
{
    diag_log_platform_clear();
}

bool diag_log_is_dumping(void)
{
    return diag_log_get_dumping() || diag_log_platform_is_dumping();
}

uint32_t diag_log_read_range(uint32_t offset, uint32_t limit,
                              void *buffer, uint32_t buffer_size)
{
    return diag_log_platform_read_range(offset, limit, buffer, buffer_size);
}

static void diag_log_print_sources(void)
{
    uint32_t mask = diag_log_source_mask();
    for (size_t i = 0; i < sizeof(s_source_defs) / sizeof(s_source_defs[0]); ++i) {
        const diag_log_source_def_t *def = &s_source_defs[i];
        uint32_t bit = diag_log_source_bit(def->source);
        printf(
            "~DIAGLOG:SOURCE name=%s id=0x%02x info_enabled=%u info_default=%u warn_error_always=1 mask=0x%08" PRIx32 "\n",
            def->name,
            (unsigned)def->source,
            (mask & bit) != 0 ? 1u : 0u,
            def->info_default_enabled ? 1u : 0u,
            mask);
    }
    fflush(stdout);
}

static const char *diag_log_skip_separator(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    while (*text == ':' || *text == ' ') {
        text++;
    }
    return text;
}

static bool diag_log_parse_source_token(const char *text, uint16_t *out_source)
{
    if (text == NULL || out_source == NULL || *text == '\0') {
        return false;
    }

    char token[24] = {0};
    size_t len = 0;
    while (text[len] != '\0' && text[len] != '\r' && text[len] != '\n' &&
           text[len] != ':' && text[len] != ' ') {
        if (len + 1 >= sizeof(token)) {
            return false;
        }
        char c = text[len];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        token[len] = c;
        len++;
    }

    if (len == 0) {
        return false;
    }

    return diag_log_source_from_name(token, out_source);
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
        diag_log_dump();
        return true;
    }
    if (strcmp(cmd_buffer, "COUNT") == 0) {
        ESP_LOGI(TAG, "DIAGLOG COUNT: %" PRIu32, diag_log_count());
        return true;
    }
    if (strcmp(cmd_buffer, "CLEAR") == 0) {
        diag_log_clear();
        return true;
    }
    if (strcmp(cmd_buffer, "SOURCES") == 0) {
        diag_log_print_sources();
        return true;
    }
    if (strncmp(cmd_buffer, "ENABLE", 6) == 0) {
        uint16_t source = 0;
        if (diag_log_parse_source_token(diag_log_skip_separator(cmd_buffer + 6), &source) &&
            diag_log_source_set_enabled(source, true)) {
            ESP_LOGI(TAG, "DIAGLOG SOURCE: %s enabled", diag_log_source_name(source));
            diag_log_print_sources();
            return true;
        }
        ESP_LOGW(TAG, "DIAGLOG: unknown source for ENABLE: %s", cmd_buffer + 6);
        return true;
    }
    if (strncmp(cmd_buffer, "DISABLE", 7) == 0) {
        uint16_t source = 0;
        if (diag_log_parse_source_token(diag_log_skip_separator(cmd_buffer + 7), &source) &&
            diag_log_source_set_enabled(source, false)) {
            ESP_LOGI(TAG, "DIAGLOG SOURCE: %s disabled", diag_log_source_name(source));
            diag_log_print_sources();
            return true;
        }
        ESP_LOGW(TAG, "DIAGLOG: unknown source for DISABLE: %s", cmd_buffer + 7);
        return true;
    }
    if (strncmp(cmd_buffer, "LAST:", 5) == 0) {
        char *tail = cmd_buffer + 5;
        uint32_t n = (uint32_t)atoi(tail);
        if (n > 0) {
            char *source_sep = strchr(tail, ':');
            if (source_sep != NULL) {
                uint16_t source = 0;
                if (diag_log_parse_source_token(source_sep + 1, &source)) {
                    diag_log_dump_last_by_source(n, source);
                } else {
                    ESP_LOGW(TAG, "DIAGLOG: unknown source for LAST: %s", source_sep + 1);
                }
            } else {
                diag_log_dump_last(n);
            }
        }
        return true;
    }

    ESP_LOGW(TAG, "DIAGLOG: unknown command: %s", cmd_buffer);
    return true;
}
