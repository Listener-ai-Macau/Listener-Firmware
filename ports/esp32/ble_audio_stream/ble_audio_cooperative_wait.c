#include "ble_audio_cooperative_wait.h"

#include <inttypes.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ble_audio_wait";
static portMUX_TYPE s_owner_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_owner;
static bool s_init_started;
static bool s_disabled = true;
static bool s_success_reported;
static SemaphoreHandle_t s_elapsed;
static esp_timer_handle_t s_timer;

static void elapsed_callback(void *argument)
{
    /* The semaphore remains alive for the export worker's entire lifetime,
     * including after a timeout. A late callback never targets a stack object. */
    portENTER_CRITICAL(&s_owner_lock);
    const TaskHandle_t owner = s_owner;
    portEXIT_CRITICAL(&s_owner_lock);
    /* A high-priority task may preempt the worker between timer arm and
     * semaphore take. Giving an already elapsed token there would remove the
     * real Blocked interval which protects IDLE0. Reject that early expiry;
     * the bounded waiter then falls back to the original tick blocker. */
    if (owner != NULL && eTaskGetState(owner) == eBlocked) {
        (void)xSemaphoreGive((SemaphoreHandle_t)argument);
    }
}

static void coarse_block(uint32_t delay_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    vTaskDelay(ticks == 0 ? 1 : ticks);
}

void ble_audio_cooperative_wait_init(void)
{
    portENTER_CRITICAL(&s_owner_lock);
    if (s_init_started) {
        portEXIT_CRITICAL(&s_owner_lock);
        return;
    }
    s_init_started = true;
    portEXIT_CRITICAL(&s_owner_lock);

    s_elapsed = xSemaphoreCreateBinary();
    esp_err_t result = ESP_ERR_NO_MEM;
    if (s_elapsed != NULL) {
        const esp_timer_create_args_t arguments = {
            .callback = elapsed_callback,
            .arg = s_elapsed,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "audio_idle_block",
            .skip_unhandled_events = true,
        };
        result = esp_timer_create(&arguments, &s_timer);
        if (result == ESP_OK) {
            s_disabled = false;
        } else {
            vSemaphoreDelete(s_elapsed);
            s_elapsed = NULL;
        }
    }
    if (s_disabled) {
        ESP_LOGW(TAG, "precise cooperative block unavailable; retaining tick block: %s",
                 esp_err_to_name(result));
    }
    /* Publish only complete resources. Foreign callers use the coarse path
     * without inspecting or changing this worker's timer/semaphore state. */
    portENTER_CRITICAL(&s_owner_lock);
    s_owner = xTaskGetCurrentTaskHandle();
    portEXIT_CRITICAL(&s_owner_lock);
}

static void disable_precise_block(const char *reason)
{
    s_disabled = true;
    (void)esp_timer_stop(s_timer);
    /* Never re-arm after a timeout/start error: an already-dispatched callback
     * can still give the semaphore, but cannot satisfy another precise wait. */
    ESP_LOGW(TAG, "precise cooperative block disabled; retaining tick block: %s", reason);
}

void ble_audio_cooperative_wait_ms(uint32_t delay_ms)
{
    if (delay_ms == 0) {
        return;
    }
    portENTER_CRITICAL(&s_owner_lock);
    const TaskHandle_t owner = s_owner;
    portEXIT_CRITICAL(&s_owner_lock);
    if (owner == NULL || owner != xTaskGetCurrentTaskHandle() ||
        delay_ms >= portTICK_PERIOD_MS || s_disabled) {
        coarse_block(delay_ms);
        return;
    }

    if (xSemaphoreTake(s_elapsed, 0) == pdTRUE) {
        disable_precise_block("unexpected elapsed token");
        coarse_block(delay_ms);
        return;
    }
    const int64_t began_us = esp_timer_get_time();
    if (esp_timer_start_once(s_timer, (uint64_t)delay_ms * 1000U) != ESP_OK) {
        disable_precise_block("timer start failed");
        coarse_block(delay_ms);
        return;
    }
    /* A callback failure must not leave the audio worker blocked indefinitely.
     * Two coarse ticks include a complete tick beyond the sub-tick deadline.
     * This is a resource fallback, never recording silence/STOP evidence. */
    if (xSemaphoreTake(s_elapsed, pdMS_TO_TICKS(delay_ms) + 2) != pdTRUE) {
        disable_precise_block("elapsed callback missing");
        coarse_block(delay_ms);
        return;
    }
    if (!s_success_reported) {
        s_success_reported = true;
        ESP_LOGI(TAG, "precise cooperative block active: requested_ms=%" PRIu32
                 " actual_us=%" PRIi64, delay_ms, esp_timer_get_time() - began_us);
    }
}
