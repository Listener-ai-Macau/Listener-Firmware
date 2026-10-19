/* Board-owned, atomic NVS profile. Partial uploads never become live settings. */
#include "device_key_profile.h"
#include "cJSON.h"
#include "device_settings.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t magic, revision, length, crc;
    uint8_t data[DEVICE_KEY_PROFILE_MAX];
} record_t;
static record_t saved, pending;
static uint8_t live_hid[15][3];
static uint32_t transaction, expected_revision, received, page_offset;
static bool uploading;
static TickType_t last_upload;
static char device_uid[13];
static StaticSemaphore_t lock_storage;
static SemaphoreHandle_t lock;
static const uint32_t MAGIC = 0x31504b44; /* DKP1 */
static uint32_t crc32_bytes(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
static bool valid_profile(const record_t *r, bool apply) {
    if (r->magic != MAGIC || r->length == 0 || r->length > DEVICE_KEY_PROFILE_MAX ||
        r->crc != crc32_bytes(r->data, r->length))
        return false;
    cJSON *root = cJSON_ParseWithLength((const char *)r->data, r->length);
    if (!root)
        return false;
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON *hid = cJSON_GetObjectItemCaseSensitive(root, "hid");
    bool ok = cJSON_IsNumber(schema) && schema->valueint == 1 && cJSON_IsArray(hid) &&
              cJSON_GetArraySize(hid) == 15;
    uint8_t compiled[15][3] = {{0}};
    for (unsigned i = 0; ok && i < 15; i++) {
        cJSON *item = cJSON_GetArrayItem(hid, i);
        if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != 3) {
            ok = false;
            break;
        }
        for (unsigned j = 0; j < 3; j++) {
            cJSON *v = cJSON_GetArrayItem(item, j);
            if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint || v->valueint < 0 ||
                v->valueint > 255) {
                ok = false;
                break;
            }
            compiled[i][j] = (uint8_t)v->valueint;
        }
        if (compiled[i][0] > 2)
            ok = false;
    }
    if (ok && apply)
        memcpy(live_hid, compiled, sizeof(live_hid));
    cJSON_Delete(root);
    return ok;
}
static esp_err_t persist(void *unused) {
    (void)unused;
    nvs_handle_t nvs;
    esp_err_t result = nvs_open("device", NVS_READWRITE, &nvs);
    if (result != ESP_OK)
        return result;
    result = nvs_set_blob(nvs, "key_profile", &pending, 16 + pending.length);
    if (result == ESP_OK)
        result = nvs_commit(nvs);
    nvs_close(nvs);
    return result;
}
void device_key_profile_init(void) {
    if (lock)
        return; /* called once during device_settings_init, before BLE */
    lock = xSemaphoreCreateMutexStatic(&lock_storage);
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) == ESP_OK)
        snprintf(device_uid, sizeof(device_uid), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    saved.revision = 1;
    nvs_handle_t nvs;
    size_t size = sizeof(saved);
    if (nvs_open("device", NVS_READONLY, &nvs) == ESP_OK) {
        esp_err_t result = nvs_get_blob(nvs, "key_profile", &saved, &size);
        nvs_close(nvs);
        if (result != ESP_OK || size < 16 || size != 16 + saved.length || saved.revision == 0 ||
            !valid_profile(&saved, true)) {
            memset(&saved, 0, sizeof(saved));
            saved.revision = 1;
        }
    }
}
static int unhex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
esp_err_t device_key_profile_command(const char *cmd) {
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (uploading && xTaskGetTickCount() - last_upload > pdMS_TO_TICKS(30000))
        uploading = false;
    uint32_t rev, len, crc, tx, off;
    int end = 0;
    char hex[33];
    if (sscanf(cmd, "B %" SCNx32 " %" SCNx32 " %" SCNx32 " %" SCNx32 "%n", &rev, &len, &crc, &tx,
               &end) == 4 &&
        cmd[end] == 0) {
        /* Do not allow another client to replace an unfinished transaction. */
        if (rev == saved.revision && len > 0 && len <= DEVICE_KEY_PROFILE_MAX && tx != 0 &&
            (!uploading || transaction == tx)) {
            memset(&pending, 0, sizeof(pending));
            pending.magic = MAGIC;
            pending.length = len;
            pending.crc = crc;
            expected_revision = rev;
            transaction = tx;
            received = 0;
            uploading = true;
            result = ESP_OK;
        } else
            result = ESP_ERR_INVALID_STATE;
    } else if (sscanf(cmd, "D %" SCNx32 " %" SCNx32 " %32s%n", &tx, &off, hex, &end) == 3 &&
               cmd[end] == 0) {
        size_t bytes = strlen(hex) / 2;
        if (uploading && transaction == tx && strlen(hex) % 2 == 0 && bytes > 0 &&
            off <= received && off + bytes <= pending.length) {
            bool ok = true;
            for (size_t i = 0; i < bytes; i++)
                if (unhex(hex[2 * i]) < 0 || unhex(hex[2 * i + 1]) < 0)
                    ok = false;
            if (ok && off < received && off + bytes > received)
                ok = false;
            for (size_t i = 0; ok && i < bytes; i++) {
                uint8_t b = (uint8_t)((unhex(hex[2 * i]) << 4) | unhex(hex[2 * i + 1]));
                if (off < received && pending.data[off + i] != b)
                    ok = false;
            }
            if (ok) {
                for (size_t i = 0; i < bytes; i++)
                    pending.data[off + i] =
                        (uint8_t)((unhex(hex[2 * i]) << 4) | unhex(hex[2 * i + 1]));
                if (off == received)
                    received += bytes;
                result = ESP_OK;
            }
        }
    } else if (sscanf(cmd, "C %" SCNx32 "%n", &tx, &end) == 1 && cmd[end] == 0) {
        if (uploading && tx == transaction && saved.revision == expected_revision &&
            received == pending.length && valid_profile(&pending, false)) {
            pending.revision = saved.revision == UINT32_MAX ? 1 : saved.revision + 1;
            result = device_settings_run_flash_job(persist, NULL);
            if (result == ESP_OK) {
                saved = pending;
                (void)valid_profile(&saved, true);
                page_offset = 0;
                uploading = false;
            }
        } else
            result = ESP_ERR_INVALID_STATE;
    } else if (sscanf(cmd, "R %" SCNx32 "%n", &off, &end) == 1 && cmd[end] == 0) {
        if (off <= saved.length) {
            page_offset = off;
            result = ESP_OK;
        }
    } else if (sscanf(cmd, "X %" SCNx32 "%n", &tx, &end) == 1 && cmd[end] == 0) {
        if (uploading && transaction == tx) {
            uploading = false;
            result = ESP_OK;
        }
    }
    if (result == ESP_OK && uploading)
        last_upload = xTaskGetTickCount();
    xSemaphoreGive(lock);
    return result;
}
bool device_key_profile_format(char *out, size_t size, char sep) {
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE)
        return false;
    char hex[129];
    size_t bytes = saved.length > page_offset ? saved.length - page_offset : 0;
    if (bytes > 64)
        bytes = 64;
    for (size_t i = 0; i < bytes; i++)
        snprintf(hex + 2 * i, 3, "%02x", saved.data[page_offset + i]);
    hex[bytes * 2] = 0;
    int n =
        snprintf(out, size,
                 "%cki=%s%ckr=%" PRIu32 "%cks=%" PRIu32 "%ckc=%08" PRIx32 "%cko=%" PRIu32 "%ckd=%s",
                 sep, device_uid, sep, saved.revision, sep, saved.length, sep, saved.crc, sep,
                 page_offset, sep, hex);
    xSemaphoreGive(lock);
    return n >= 0 && (size_t)n < size;
}
uint8_t device_key_profile_hid(uint8_t key, uint8_t gesture, uint8_t *usage, uint8_t *modifier) {
    if (!lock || key < 1 || key > 5 || gesture > 2 ||
        xSemaphoreTake(lock, pdMS_TO_TICKS(30)) != pdTRUE)
        return lock == NULL ? 0 : 1; /* never route a busy board through a global fallback */
    const uint8_t *p = live_hid[gesture * 5 + key - 1];
    uint8_t kind = p[0];
    if (kind == 0 && saved.length > 0)
        kind = 3;
    *modifier = p[1];
    *usage = p[2];
    xSemaphoreGive(lock);
    return kind;
}

bool device_key_profile_event(char *out, size_t size, uint8_t key, uint8_t gesture) {
    if (!lock || key < 1 || key > 5 || gesture > 2 ||
        xSemaphoreTake(lock, pdMS_TO_TICKS(30)) != pdTRUE)
        return false;
    int n = snprintf(out, size, "DKEY:%s:%u:%u:%" PRIu32, device_uid, key, gesture, saved.revision);
    xSemaphoreGive(lock);
    return n > 0 && (size_t)n < size;
}
