#include "watchdog_platform.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_attr.h"
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
#define WATCHDOG_PLATFORM_RTC_MAGIC 0x57445452U
#define WATCHDOG_PLATFORM_TASK_NAME_BYTES 24U
#define WATCHDOG_PLATFORM_TRIGGERED_BYTES 96U

static const char *TAG = "watchdog";

typedef struct {
    uint32_t magic;
    uint32_t timeout_count;
    uint32_t failed_core_mask;
    uint32_t triggered_length;
    char triggered[WATCHDOG_PLATFORM_TRIGGERED_BYTES];
    char running_cpu0[WATCHDOG_PLATFORM_TASK_NAME_BYTES];
    char running_cpu1[WATCHDOG_PLATFORM_TASK_NAME_BYTES];
    uint32_t checksum;
} watchdog_platform_rtc_timeout_t;

static RTC_NOINIT_ATTR watchdog_platform_rtc_timeout_t s_rtc_timeout;

static IRAM_ATTR uint32_t watchdog_platform_rtc_checksum(
    const watchdog_platform_rtc_timeout_t *record)
{
    const uint8_t *cursor = (const uint8_t *)&record->timeout_count;
    const uint8_t *end = (const uint8_t *)&record->checksum;
    uint32_t checksum = 2166136261U;
    while (cursor < end) {
        checksum ^= *cursor++;
        checksum *= 16777619U;
    }
    return checksum;
}

static IRAM_ATTR void watchdog_platform_copy_task_name(
    char *destination,
    size_t destination_bytes,
    TaskHandle_t task)
{
    if (destination == NULL || destination_bytes == 0U) {
        return;
    }
    const char *name = task != NULL ? pcTaskGetName(task) : "none";
    size_t index = 0U;
    while (index + 1U < destination_bytes && name[index] != '\0') {
        destination[index] = name[index];
        index++;
    }
    destination[index] = '\0';
}

typedef struct {
    bool skip_caption;
} watchdog_platform_wdt_capture_t;

static IRAM_ATTR void watchdog_platform_capture_wdt_message(
    void *opaque,
    const char *message)
{
    watchdog_platform_wdt_capture_t *capture =
        (watchdog_platform_wdt_capture_t *)opaque;
    if (capture == NULL || message == NULL) {
        return;
    }
    if (capture->skip_caption) {
        capture->skip_caption = false;
        return;
    }
    uint32_t index = s_rtc_timeout.triggered_length;
    while (index + 1U < WATCHDOG_PLATFORM_TRIGGERED_BYTES &&
           *message != '\0') {
        s_rtc_timeout.triggered[index++] = *message++;
    }
    s_rtc_timeout.triggered[index] = '\0';
    s_rtc_timeout.triggered_length = index;
}

/*
 * ESP-IDF invokes this weak user hook from the Task WDT ISR immediately before
 * panic reset. Keep the writes in RTC no-init memory and avoid normal logging:
 * the next boot can then name the missed subscribers and running tasks even
 * when no serial monitor was attached at the failure.
 */
void IRAM_ATTR esp_task_wdt_isr_user_handler(void)
{
    uint32_t previous_count =
        s_rtc_timeout.magic == WATCHDOG_PLATFORM_RTC_MAGIC
            ? s_rtc_timeout.timeout_count
            : 0U;
    s_rtc_timeout.magic = 0U;
    s_rtc_timeout.timeout_count = previous_count + 1U;
    s_rtc_timeout.failed_core_mask = 0U;
    s_rtc_timeout.triggered_length = 0U;
    s_rtc_timeout.triggered[0] = '\0';
    s_rtc_timeout.running_cpu0[0] = '\0';
    s_rtc_timeout.running_cpu1[0] = '\0';

    watchdog_platform_wdt_capture_t capture = {
        .skip_caption = true,
    };
    int failed_cores = 0;
    (void)esp_task_wdt_print_triggered_tasks(
        watchdog_platform_capture_wdt_message,
        &capture,
        &failed_cores);
    s_rtc_timeout.failed_core_mask = (uint32_t)failed_cores;
    watchdog_platform_copy_task_name(
        s_rtc_timeout.running_cpu0,
        sizeof(s_rtc_timeout.running_cpu0),
        xTaskGetCurrentTaskHandleForCore(0));
#if !CONFIG_FREERTOS_UNICORE
    watchdog_platform_copy_task_name(
        s_rtc_timeout.running_cpu1,
        sizeof(s_rtc_timeout.running_cpu1),
        xTaskGetCurrentTaskHandleForCore(1));
#endif
    s_rtc_timeout.checksum =
        watchdog_platform_rtc_checksum(&s_rtc_timeout);
    s_rtc_timeout.magic = WATCHDOG_PLATFORM_RTC_MAGIC;
}

static bool watchdog_platform_rtc_timeout_valid(void)
{
    return s_rtc_timeout.magic == WATCHDOG_PLATFORM_RTC_MAGIC &&
           s_rtc_timeout.checksum ==
               watchdog_platform_rtc_checksum(&s_rtc_timeout);
}

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
    if (watchdog_platform_rtc_timeout_valid()) {
        ESP_LOGE(
            TAG,
            "retained timeout: count=%" PRIu32
            " failed_core_mask=0x%08" PRIx32
            " missed=\"%s\" running_cpu0=\"%s\" running_cpu1=\"%s\"",
            s_rtc_timeout.timeout_count,
            s_rtc_timeout.failed_core_mask,
            s_rtc_timeout.triggered,
            s_rtc_timeout.running_cpu0,
            s_rtc_timeout.running_cpu1);
    } else {
        ESP_LOGI(TAG, "retained timeout: none");
    }
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
