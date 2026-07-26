#include "watchdog_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP_TASK_WDT_EN
#define CONFIG_ESP_TASK_WDT_EN 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_INIT
#define CONFIG_ESP_TASK_WDT_INIT 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_PANIC
#define CONFIG_ESP_TASK_WDT_PANIC 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_TIMEOUT_S
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
#define CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 0
#endif
#ifndef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
#define CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 0
#endif
#ifndef CONFIG_ESP_INT_WDT
#define CONFIG_ESP_INT_WDT 0
#endif
#ifndef CONFIG_ESP_INT_WDT_TIMEOUT_MS
#define CONFIG_ESP_INT_WDT_TIMEOUT_MS 0
#endif

#define WATCHDOG_PLATFORM_FEED_INTERVAL_MS 1000U
#define WATCHDOG_PLATFORM_SHUTDOWN_CRITICAL_TIMEOUT_MS 30000U
#define WATCHDOG_PLATFORM_IDLE_CORE_MASK \
    ((CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 ? (1U << 0) : 0U) | \
     (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 ? (1U << 1) : 0U))
#define WATCHDOG_PLATFORM_USB_PREFIX "WDT:"

static const char *TAG = "watchdog";

esp_err_t watchdog_platform_subscribe_current_task(const char *task_name)
{
#if CONFIG_ESP_TASK_WDT_EN
    esp_err_t status = esp_task_wdt_status(NULL);
    if (status == ESP_OK) {
        return ESP_OK;
    }
    if (status == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "task watchdog not initialized; task=%s", task_name != NULL ? task_name : "unknown");
        return status;
    }

    esp_err_t ret = esp_task_wdt_add(NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "task subscribed: %s timeout_s=%u panic=%u",
            task_name != NULL ? task_name : "unknown",
            (unsigned)CONFIG_ESP_TASK_WDT_TIMEOUT_S,
            (unsigned)CONFIG_ESP_TASK_WDT_PANIC);
        (void)esp_task_wdt_reset();
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "task watchdog subscribe failed: task=%s ret=%s",
        task_name != NULL ? task_name : "unknown",
        esp_err_to_name(ret));
    return ret;
#else
    (void)task_name;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void watchdog_platform_feed_current_task(void)
{
#if CONFIG_ESP_TASK_WDT_EN
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        (void)esp_task_wdt_reset();
    }
#endif
}

void watchdog_platform_delay_ms(uint32_t delay_ms)
{
    uint32_t remaining_ms = delay_ms;
    while (remaining_ms > 0) {
        uint32_t chunk_ms = remaining_ms > WATCHDOG_PLATFORM_FEED_INTERVAL_MS
            ? WATCHDOG_PLATFORM_FEED_INTERVAL_MS
            : remaining_ms;
        vTaskDelay(pdMS_TO_TICKS(chunk_ms));
        watchdog_platform_feed_current_task();
        remaining_ms -= chunk_ms;
    }
}

uint32_t watchdog_platform_task_notify_take(BaseType_t clear_on_exit, uint32_t wait_ms)
{
    uint32_t remaining_ms = wait_ms;
    while (remaining_ms > 0) {
        uint32_t chunk_ms = remaining_ms > WATCHDOG_PLATFORM_FEED_INTERVAL_MS
            ? WATCHDOG_PLATFORM_FEED_INTERVAL_MS
            : remaining_ms;
        uint32_t notified = ulTaskNotifyTake(clear_on_exit, pdMS_TO_TICKS(chunk_ms));
        watchdog_platform_feed_current_task();
        if (notified != 0) {
            return notified;
        }
        remaining_ms -= chunk_ms;
    }
    return 0;
}

uint32_t watchdog_platform_task_notify_take_low_power(BaseType_t clear_on_exit, uint32_t wait_ms)
{
#if CONFIG_ESP_TASK_WDT_EN
    bool was_subscribed = esp_task_wdt_status(NULL) == ESP_OK;
    if (was_subscribed) {
        esp_err_t ret = esp_task_wdt_delete(NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "low-power wait WDT unsubscribe failed: %s", esp_err_to_name(ret));
            return watchdog_platform_task_notify_take(clear_on_exit, wait_ms);
        }
    }
#else
    bool was_subscribed = false;
#endif

    uint32_t notified = ulTaskNotifyTake(clear_on_exit, pdMS_TO_TICKS(wait_ms));

#if CONFIG_ESP_TASK_WDT_EN
    if (was_subscribed) {
        esp_err_t ret = esp_task_wdt_add(NULL);
        if (ret == ESP_OK) {
            (void)esp_task_wdt_reset();
        } else {
            ESP_LOGW(TAG, "low-power wait WDT resubscribe failed: %s", esp_err_to_name(ret));
        }
    }
#endif
    return notified;
}

static esp_err_t watchdog_platform_reconfigure(uint32_t timeout_ms, bool trigger_panic, const char *mode)
{
#if CONFIG_ESP_TASK_WDT_EN
    esp_task_wdt_config_t config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = WATCHDOG_PLATFORM_IDLE_CORE_MASK,
        .trigger_panic = trigger_panic,
    };
    esp_err_t ret = esp_task_wdt_reconfigure(&config);
    if (ret == ESP_OK) {
        ESP_LOGW(
            TAG,
            "task watchdog reconfigured: mode=%s timeout_ms=%" PRIu32 " panic=%u idle_core_mask=0x%08" PRIx32,
            mode != NULL ? mode : "unknown",
            timeout_ms,
            trigger_panic ? 1u : 0u,
            (uint32_t)WATCHDOG_PLATFORM_IDLE_CORE_MASK);
    } else {
        ESP_LOGW(
            TAG,
            "task watchdog reconfigure failed: mode=%s ret=%s",
            mode != NULL ? mode : "unknown",
            esp_err_to_name(ret));
    }
    return ret;
#else
    (void)timeout_ms;
    (void)trigger_panic;
    (void)mode;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t watchdog_platform_enter_shutdown_critical(const char *reason)
{
    return watchdog_platform_reconfigure(
        WATCHDOG_PLATFORM_SHUTDOWN_CRITICAL_TIMEOUT_MS,
        CONFIG_ESP_TASK_WDT_PANIC != 0,
        reason != NULL ? reason : "shutdown_critical");
}

esp_err_t watchdog_platform_exit_shutdown_critical(void)
{
    return watchdog_platform_reconfigure(
        CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U,
        CONFIG_ESP_TASK_WDT_PANIC != 0,
        "normal");
}

void watchdog_platform_log_config(void)
{
    ESP_LOGI(
        TAG,
        "config: task_wdt=%u init=%u panic=%u timeout_s=%u int_wdt=%u int_timeout_ms=%u",
        (unsigned)CONFIG_ESP_TASK_WDT_EN,
        (unsigned)CONFIG_ESP_TASK_WDT_INIT,
        (unsigned)CONFIG_ESP_TASK_WDT_PANIC,
        (unsigned)CONFIG_ESP_TASK_WDT_TIMEOUT_S,
        (unsigned)CONFIG_ESP_INT_WDT,
        (unsigned)CONFIG_ESP_INT_WDT_TIMEOUT_MS);
}

static void watchdog_platform_deadlock_for_test(void)
{
    volatile uint32_t spin_count = 0;

    ESP_LOGE(TAG, "WDT DEADLOCK test command accepted; spinning without feed");
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        spin_count++;
    }
}

bool watchdog_platform_consume_usb_command(const char *line)
{
    if (line == NULL) {
        return false;
    }
    if (*line == '~') {
        line++;
    }

    const size_t prefix_len = strlen(WATCHDOG_PLATFORM_USB_PREFIX);
    if (strncmp(line, WATCHDOG_PLATFORM_USB_PREFIX, prefix_len) != 0) {
        return false;
    }

    const char *command = line + prefix_len;
    if (strcmp(command, "STATUS") == 0) {
        watchdog_platform_log_config();
        return true;
    }

    if (strcmp(command, "DEADLOCK") == 0) {
        watchdog_platform_deadlock_for_test();
        return true;
    }

    ESP_LOGW(TAG, "WDT unknown command: %s", command);
    return true;
}

BaseType_t watchdog_platform_start_task_on_spiram(
    TaskFunction_t task_func,
    const char *name,
    uint32_t stack_words,
    UBaseType_t priority,
    TaskHandle_t *handle_out,
    StaticTask_t *task_control,
    StackType_t **task_stack)
{
    if (*task_stack == NULL) {
        *task_stack = (StackType_t *)heap_caps_malloc(
            (size_t)stack_words * sizeof(StackType_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (*task_stack == NULL) {
        return pdFAIL;
    }
    *handle_out = xTaskCreateStatic(
        task_func,
        name,
        stack_words,
        NULL,
        priority,
        *task_stack,
        task_control);
    if (*handle_out == NULL) {
        heap_caps_free(*task_stack);
        *task_stack = NULL;
        return pdFAIL;
    }
    return pdPASS;
}
