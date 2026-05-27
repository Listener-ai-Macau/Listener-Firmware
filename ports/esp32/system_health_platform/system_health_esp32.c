#include "system_health_platform.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "diag_log.h"
#include "ble_hid.h"
#include "audio_capture.h"
#include "keyboard.h"
#include "voice_recording_control.h"

static const char *TAG = "health";

#define HEALTH_INTERVAL_S       60
#ifndef CONFIG_POWER_MANAGER_LOW_POWER_HEALTH_INTERVAL_S
#define CONFIG_POWER_MANAGER_LOW_POWER_HEALTH_INTERVAL_S 600
#endif
#define HEALTH_TASK_STACK_BYTES (4 * 1024)
#define HEALTH_HEAP_WARN_KB     20U
#define HEALTH_BLE_DISCONNECT_RATE_WINDOW_S 60U
#define HEALTH_BLE_DISCONNECT_RATE_LIMIT    2U

static TaskHandle_t s_health_task;
static bool s_low_power_mode;

static void system_health_task(void *parameter)
{
    (void)parameter;

    uint32_t last_disconnect_count = 0;
    uint32_t window_start_ms = 0;

    while (1) {
        uint32_t interval_s = s_low_power_mode
            ? (uint32_t)CONFIG_POWER_MANAGER_LOW_POWER_HEALTH_INTERVAL_S
            : HEALTH_INTERVAL_S;
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_s * 1000U));

        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t heap_min = esp_get_minimum_free_heap_size();
        bool ble_connected = ble_hid_is_connected();
        uint32_t disconnects = ble_hid_get_disconnect_count();
        uint32_t audio_frames = audio_capture_get_frame_count();
        uint32_t audio_drops = audio_capture_get_dropped_frame_count();
        uint32_t keys = keyboard_get_key_press_count();
        uint32_t sessions = voice_recording_control_get_session_count();
        uint32_t uptime_s = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

        ESP_LOGI(TAG,
                 "heartbeat: uptime=%" PRIu32 "s heap_free=%" PRIu32 "KB heap_min=%" PRIu32 "KB "
                 "ble=%s disconnects=%" PRIu32 " audio_frames=%" PRIu32 " audio_drops=%" PRIu32 " "
                 "keys=%" PRIu32 " sessions=%" PRIu32,
                 uptime_s,
                 heap_free / 1024, heap_min / 1024,
                 ble_connected ? "OK" : "OFF",
                 disconnects,
                 audio_frames, audio_drops,
                 keys, sessions);

        diag_log(DIAG_SRC_HEALTH, DIAG_HEALTH_HEARTBEAT, DIAG_SEV_INFO,
                 heap_free / 1024, heap_min / 1024, ble_connected ? 1 : 0, uptime_s / 60);

        /* Heap alert */
        if (heap_free / 1024 < HEALTH_HEAP_WARN_KB) {
            ESP_LOGW(TAG, "heap pressure: free=%" PRIu32 " below %" PRIu32 "KB threshold",
                     heap_free / 1024, HEALTH_HEAP_WARN_KB);
            diag_log(DIAG_SRC_HEALTH, DIAG_HEALTH_ALERT, DIAG_SEV_WARN,
                     1, heap_free / 1024, HEALTH_HEAP_WARN_KB, 0);
        }

        /* BLE disconnect rate alert */
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint32_t window_disconnects = disconnects - last_disconnect_count;
        if (now_ms - window_start_ms >= HEALTH_BLE_DISCONNECT_RATE_WINDOW_S * 1000) {
            if (window_disconnects > HEALTH_BLE_DISCONNECT_RATE_LIMIT) {
                ESP_LOGW(TAG, "ble unstable: %" PRIu32 " disconnects in %" PRIu32 "s (>2/min)",
                         window_disconnects, HEALTH_BLE_DISCONNECT_RATE_WINDOW_S);
                diag_log(DIAG_SRC_HEALTH, DIAG_HEALTH_ALERT, DIAG_SEV_WARN,
                         2, window_disconnects, HEALTH_BLE_DISCONNECT_RATE_LIMIT, 0);
            }
            last_disconnect_count = disconnects;
            window_start_ms = now_ms;
        }
    }
}

void system_health_platform_set_low_power_mode(bool enabled)
{
    if (s_low_power_mode == enabled) {
        return;
    }

    s_low_power_mode = enabled;
    if (s_health_task != NULL) {
        xTaskNotifyGive(s_health_task);
    }
    ESP_LOGI(
        TAG,
        "system health low-power mode=%u interval_s=%" PRIu32,
        enabled ? 1u : 0u,
        enabled ? (uint32_t)CONFIG_POWER_MANAGER_LOW_POWER_HEALTH_INTERVAL_S : (uint32_t)HEALTH_INTERVAL_S);
}

esp_err_t system_health_platform_init(void)
{
    return ESP_OK;
}

esp_err_t system_health_platform_start(void)
{
    if (s_health_task != NULL) {
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        system_health_task,
        "health_task",
        HEALTH_TASK_STACK_BYTES,
        NULL,
        configMAX_PRIORITIES - 5,
        &s_health_task);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "health task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "system health monitoring started: interval_s=%d", HEALTH_INTERVAL_S);
    return ESP_OK;
}
