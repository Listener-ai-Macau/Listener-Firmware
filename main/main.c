#include "keyboard.h"
#include "ble_hid.h"
#include "board.h"
#include "boot_safety.h"
#include "self_test.h"
#include "system_health.h"
#include "diag_log.h"
#include "firmware_ota.h"
#include "power_manager.h"
#include "watchdog_platform.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include <inttypes.h>

static const char *TAG = "app_main";

static power_manager_wake_source_t map_esp_wakeup_cause(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
        return POWER_MANAGER_WAKE_SOURCE_EXT0;
    case ESP_SLEEP_WAKEUP_EXT1:
        return POWER_MANAGER_WAKE_SOURCE_EXT1;
    case ESP_SLEEP_WAKEUP_TIMER:
        return POWER_MANAGER_WAKE_SOURCE_TIMER;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return POWER_MANAGER_WAKE_SOURCE_TOUCHPAD;
    case ESP_SLEEP_WAKEUP_ULP:
        return POWER_MANAGER_WAKE_SOURCE_ULP;
    case ESP_SLEEP_WAKEUP_GPIO:
        return POWER_MANAGER_WAKE_SOURCE_GPIO;
    case ESP_SLEEP_WAKEUP_UART:
        return POWER_MANAGER_WAKE_SOURCE_UART;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        return esp_reset_reason() == ESP_RST_DEEPSLEEP
            ? POWER_MANAGER_WAKE_SOURCE_UNDEFINED
            : POWER_MANAGER_WAKE_SOURCE_POWER_ON;
    }
}

static void configure_power_management(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#endif
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret == ESP_OK) {
        ESP_LOGI(
            TAG,
            "ESP power management enabled: max=%uMHz min=%uMHz light_sleep=%u",
            (unsigned)pm_config.max_freq_mhz,
            (unsigned)pm_config.min_freq_mhz,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            pm_config.light_sleep_enable ? 1u : 0u
#else
            0u
#endif
        );
    } else {
        ESP_LOGW(TAG, "ESP power management configure failed: %s", esp_err_to_name(ret));
    }
#else
    ESP_LOGW(TAG, "ESP power management disabled by sdkconfig");
#endif
}

static void log_power_boot_diagnostics(void)
{
    esp_sleep_wakeup_cause_t esp_wake_source = esp_sleep_get_wakeup_cause();
    power_manager_wake_source_t wake_source = map_esp_wakeup_cause(esp_wake_source);
    uint32_t wake_gpio_mask_low = (uint32_t)(esp_sleep_get_ext1_wakeup_status() & 0xffffffffu);

    diag_log(DIAG_SRC_POWER, DIAG_POWER_WAKE, DIAG_SEV_INFO,
             (uint32_t)wake_source, wake_gpio_mask_low, 0, 0);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_STATUS, DIAG_SEV_INFO,
             0, 0, 0, wake_gpio_mask_low);
    ESP_LOGI(TAG, "power wake status: esp_wake_source=%u wake_source=%u (%s) wake_gpio_mask_low=0x%08" PRIx32,
             (unsigned)esp_wake_source,
             (unsigned)wake_source,
             power_manager_wake_source_name(wake_source),
             wake_gpio_mask_low);
}

void app_main(void)
{
    self_test_init();
    diag_log_init();
    firmware_ota_init();
    configure_power_management();
    watchdog_platform_log_config();
    boot_safety_init();
    board_log_v2_diagnostics();
    log_power_boot_diagnostics();

    esp_reset_reason_t reset_reason = esp_reset_reason();
    uint32_t boot_reason = 0;
    switch (reset_reason) {
    case ESP_RST_POWERON:   boot_reason = DIAG_BOOT_POWER_ON; break;
    case ESP_RST_SW:        boot_reason = DIAG_BOOT_RESET; break;
    case ESP_RST_PANIC:     boot_reason = DIAG_BOOT_EXCEPTION; break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:   boot_reason = DIAG_BOOT_WATCHDOG; break;
    case ESP_RST_DEEPSLEEP: boot_reason = DIAG_BOOT_DEEP_SLEEP; break;
    default:                boot_reason = DIAG_BOOT_RESET; break;
    }
    diag_log(DIAG_SRC_SYSTEM, DIAG_SYS_BOOT, DIAG_SEV_INFO,
             boot_reason, 0, 0, 0);
    esp_err_t power_ret = power_manager_init();
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "power manager init failed: %s", esp_err_to_name(power_ret));
    }

    self_test_result_t post = self_test_run();
    if (!self_test_critical_ok(&post)) {
        ESP_LOGE(TAG, "POST failed; continuing in degraded mode so BLE can expose device status");
    }

    bool safe_mode = boot_safety_is_safe_mode();
    if (safe_mode) {
        ESP_LOGW(TAG, "boot safety safe mode active: BLE HID and recovery diagnostics only; audio disabled");
        ESP_LOGW(TAG, "device_status state=recovery detail=boot_safety_safe_mode");
    }

    ble_hid_set_safe_mode(safe_mode);
    esp_err_t ble_ret = ble_hid_init();
    if (ble_ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE HID init degraded: %s", esp_err_to_name(ble_ret));
    }
    esp_err_t keyboard_ret = safe_mode ? keyboard_start_safe_mode() : keyboard_start();
    if (keyboard_ret != ESP_OK) {
        ESP_LOGW(TAG, "keyboard start degraded; continuing BLE startup: %s", esp_err_to_name(keyboard_ret));
    }
    system_health_init();
    esp_err_t ble_start_ret = ble_ret == ESP_OK ? ble_hid_start() : ble_ret;
    system_health_start();
    power_ret = power_manager_start();
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "power manager start failed: %s", esp_err_to_name(power_ret));
    }
    bool ota_post_ok = self_test_critical_ok(&post);
    bool ota_ble_ready = ble_ret == ESP_OK && ble_start_ret == ESP_OK;
    bool ota_keyboard_ready = keyboard_ret == ESP_OK;
#ifdef LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL
    ESP_LOGE(TAG, "OTA rollback validation build: forcing pending-verify self-check failure");
    ota_post_ok = false;
#endif
    firmware_ota_record_self_check(ota_post_ok, ota_ble_ready, ota_keyboard_ready);
    firmware_ota_confirm_pending_verify_if_ready();
    boot_safety_start_normal_boot_clear_timer();
}
