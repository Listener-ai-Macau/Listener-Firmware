#include "boot_safety.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "diag_log.h"
#include "denzic_device_health_v1.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOOT_SAFETY_RTC_MAGIC 0x42534631u
#define BOOT_SAFETY_SAFE_MODE_THRESHOLD DENZIC_DEVICE_HEALTH_V1_SAFE_MODE_THRESHOLD
#define BOOT_SAFETY_NORMAL_CLEAR_DELAY_MS 30000u
#define BOOT_SAFETY_USB_PREFIX "BOOT:"

static const char *TAG = "boot_safety";

typedef struct {
    uint32_t magic;
    uint32_t crash_count;
    uint32_t last_reset_reason;
    uint32_t safe_mode_latched;
} boot_safety_rtc_state_t;

RTC_NOINIT_ATTR static boot_safety_rtc_state_t s_rtc_state;

static boot_safety_status_t s_status;
static TaskHandle_t s_clear_task;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static boot_safety_status_t boot_safety_status_snapshot(void)
{
    portENTER_CRITICAL(&s_status_lock);
    boot_safety_status_t status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return status;
}

static denzic_device_health_v1_reset_reason_t boot_safety_platform_reset_reason(
    esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_POWER_ON;
    case ESP_RST_EXT: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_EXTERNAL;
    case ESP_RST_SW: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_SOFTWARE;
    case ESP_RST_PANIC: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_PANIC;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_WATCHDOG;
    case ESP_RST_DEEPSLEEP: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_DEEP_SLEEP;
    case ESP_RST_BROWNOUT: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_BROWNOUT;
    case ESP_RST_USB: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_USB;
    case ESP_RST_JTAG: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_JTAG;
    case ESP_RST_PWR_GLITCH: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_POWER_GLITCH;
    case ESP_RST_CPU_LOCKUP: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_CPU_LOCKUP;
    default: return DENZIC_DEVICE_HEALTH_V1_RESET_REASON_UNKNOWN;
    }
}

const char *boot_safety_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN: return "unknown";
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "other_wdt";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "reset";
    }
}

static void boot_safety_reset_rtc_state(void)
{
    denzic_device_health_v1_boot_state_t state = {
        .crash_count = s_rtc_state.crash_count,
        .safe_mode_latched = s_rtc_state.safe_mode_latched != 0u,
    };
    denzic_device_health_v1_boot_clear(&state);
    s_rtc_state.magic = BOOT_SAFETY_RTC_MAGIC;
    s_rtc_state.crash_count = state.crash_count;
    s_rtc_state.last_reset_reason = 0;
    s_rtc_state.safe_mode_latched = state.safe_mode_latched ? 1u : 0u;
}

void boot_safety_init(void)
{
    if (s_rtc_state.magic != BOOT_SAFETY_RTC_MAGIC) {
        boot_safety_reset_rtc_state();
    }

    esp_reset_reason_t reason = esp_reset_reason();
    denzic_device_health_v1_boot_state_t platform_state = {
        .crash_count = s_rtc_state.crash_count,
        .safe_mode_latched = s_rtc_state.safe_mode_latched != 0u,
    };
    denzic_device_health_v1_boot_observe(
        &platform_state,
        boot_safety_platform_reset_reason(reason),
        BOOT_SAFETY_SAFE_MODE_THRESHOLD);
    s_rtc_state.crash_count = platform_state.crash_count;
    s_rtc_state.safe_mode_latched = platform_state.safe_mode_latched ? 1u : 0u;

    s_rtc_state.last_reset_reason = (uint32_t)reason;

    portENTER_CRITICAL(&s_status_lock);
    s_status.reset_reason = reason;
    s_status.crash_count = s_rtc_state.crash_count;
    s_status.safe_mode = s_rtc_state.safe_mode_latched != 0;
    s_status.clear_scheduled = false;
    boot_safety_status_t status = s_status;
    portEXIT_CRITICAL(&s_status_lock);

    ESP_LOGI(
        TAG,
        "status: reset_reason=%s(%u) crash_count=%" PRIu32 " threshold=%u safe_mode=%u",
        boot_safety_reset_reason_name(reason),
        (unsigned)reason,
        status.crash_count,
        (unsigned)BOOT_SAFETY_SAFE_MODE_THRESHOLD,
        status.safe_mode ? 1u : 0u);
    diag_log(
        DIAG_SRC_SYSTEM,
        DIAG_SYS_BOOT_SAFETY,
        status.safe_mode ? DIAG_SEV_WARN : DIAG_SEV_INFO,
        (uint32_t)reason,
        status.crash_count,
        status.safe_mode ? 1u : 0u,
        BOOT_SAFETY_SAFE_MODE_THRESHOLD);
}

bool boot_safety_is_safe_mode(void)
{
    return boot_safety_status_snapshot().safe_mode;
}

void boot_safety_get_status(boot_safety_status_t *status)
{
    if (status == NULL) {
        return;
    }
    *status = boot_safety_status_snapshot();
}

static void boot_safety_clear_task(void *parameter)
{
    (void)parameter;
    vTaskDelay(pdMS_TO_TICKS(BOOT_SAFETY_NORMAL_CLEAR_DELAY_MS));
    bool cleared = false;
    esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
    portENTER_CRITICAL(&s_status_lock);
    if (!s_status.safe_mode) {
        denzic_device_health_v1_boot_state_t platform_state = {
            .crash_count = s_rtc_state.crash_count,
            .safe_mode_latched = s_rtc_state.safe_mode_latched != 0u,
        };
        denzic_device_health_v1_boot_clear(&platform_state);
        s_rtc_state.crash_count = platform_state.crash_count;
        s_rtc_state.safe_mode_latched = platform_state.safe_mode_latched ? 1u : 0u;
        reset_reason = s_status.reset_reason;
        s_status.crash_count = 0;
        s_status.clear_scheduled = false;
        cleared = true;
    }
    s_clear_task = NULL;
    portEXIT_CRITICAL(&s_status_lock);
    if (cleared) {
        ESP_LOGI(TAG, "normal boot survived %ums; crash counter cleared", (unsigned)BOOT_SAFETY_NORMAL_CLEAR_DELAY_MS);
        diag_log(DIAG_SRC_SYSTEM, DIAG_SYS_BOOT_SAFETY, DIAG_SEV_INFO,
                 (uint32_t)reset_reason, 0, 0, 0);
    }
    vTaskDelete(NULL);
}

void boot_safety_start_normal_boot_clear_timer(void)
{
    portENTER_CRITICAL(&s_status_lock);
    bool already_scheduled = s_status.safe_mode || s_clear_task != NULL;
    portEXIT_CRITICAL(&s_status_lock);
    if (already_scheduled) {
        return;
    }

    BaseType_t task_ok = xTaskCreate(
        boot_safety_clear_task,
        "boot_safety_clear",
        3072,
        NULL,
        3,
        &s_clear_task);
    if (task_ok == pdPASS) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.clear_scheduled = true;
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGI(TAG, "normal boot clear timer armed: delay_ms=%u", (unsigned)BOOT_SAFETY_NORMAL_CLEAR_DELAY_MS);
    } else {
        ESP_LOGW(TAG, "normal boot clear timer task create failed");
    }
}

static void boot_safety_log_status(void)
{
    boot_safety_status_t status = boot_safety_status_snapshot();
    ESP_LOGI(
        TAG,
        "USB STATUS reset_reason=%s(%u) crash_count=%" PRIu32 " threshold=%u safe_mode=%u clear_scheduled=%u",
        boot_safety_reset_reason_name(status.reset_reason),
        (unsigned)status.reset_reason,
        status.crash_count,
        (unsigned)BOOT_SAFETY_SAFE_MODE_THRESHOLD,
        status.safe_mode ? 1u : 0u,
        status.clear_scheduled ? 1u : 0u);
}

bool boot_safety_consume_usb_command(const char *line)
{
    if (line == NULL) {
        return false;
    }
    if (*line == '~') {
        line++;
    }

    const size_t prefix_len = strlen(BOOT_SAFETY_USB_PREFIX);
    if (strncmp(line, BOOT_SAFETY_USB_PREFIX, prefix_len) != 0) {
        return false;
    }

    const char *command = line + prefix_len;
    if (strcmp(command, "STATUS") == 0) {
        boot_safety_log_status();
        return true;
    }

    if (strcmp(command, "CLEAR") == 0) {
        boot_safety_reset_rtc_state();
        portENTER_CRITICAL(&s_status_lock);
        s_status.crash_count = 0;
        s_status.safe_mode = false;
        s_status.clear_scheduled = false;
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGW(TAG, "USB CLEAR accepted; crash counter and safe mode latch cleared");
        return true;
    }

    if (strcmp(command, "CRASH") == 0) {
        ESP_LOGE(TAG, "USB CRASH accepted; restarting to exercise boot safety");
        esp_restart();
        return true;
    }

    ESP_LOGW(TAG, "BOOT unknown command: %s", command);
    return true;
}
