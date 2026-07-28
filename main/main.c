#include "keyboard.h"
#include "ble_hid.h"
#include "board.h"
#include "boot_safety.h"
#include "device_settings.h"
#include "self_test.h"
#include "system_health.h"
#include "diag_log.h"
#include "firmware_ota.h"
#include "power_manager.h"
#include "status_led.h"
#include "voice_recording_control.h"
#include "watchdog_platform.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"

#include <inttypes.h>

static const char *TAG = "app_main";

static void configure_boot_power_hold_latch(void)
{
    esp_err_t ret = board_configure_power_hold_latch();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "early PWR_HOLD/GPIO9 runtime-low setup failed: %s", esp_err_to_name(ret));
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
    esp_reset_reason_t reset_reason = esp_reset_reason();
    board_v2_power_hold_snapshot_t power_hold = {0};
    board_get_v2_power_hold_snapshot(&power_hold);
    uint32_t pwr_hold_gpio =
        power_hold.gpio >= 0 ? (uint32_t)power_hold.gpio : UINT32_MAX;

    diag_log(DIAG_SRC_POWER, DIAG_POWER_WAKE, DIAG_SEV_INFO,
             (uint32_t)reset_reason, pwr_hold_gpio, 0, 0);
    diag_log(DIAG_SRC_POWER, DIAG_POWER_STATUS, DIAG_SEV_INFO,
             0, 0, 0, pwr_hold_gpio);
    ESP_LOGI(
        TAG,
        "power cold-boot status: reset_reason=%u pwr_hold_gpio=%d pwr_hold_level=%d configured=%u policy=%s",
        (unsigned)reset_reason,
        power_hold.gpio,
        power_hold.level,
        power_hold.configured ? 1u : 0u,
        power_hold.policy != NULL ? power_hold.policy : "unknown");
}

void app_main(void)
{
    configure_boot_power_hold_latch();

    self_test_init();
    esp_err_t led_ret = status_led_init();
    if (led_ret != ESP_OK) {
        ESP_LOGW(TAG, "status LED init degraded: %s", esp_err_to_name(led_ret));
    }
    led_ret = status_led_start();
    bool status_led_ready = led_ret == ESP_OK;
    if (led_ret != ESP_OK) {
        ESP_LOGW(TAG, "status LED start degraded: %s", esp_err_to_name(led_ret));
    }
    /* Do not call status_led_show_status_window("booting") here: start() already
     * arms a single boot PWR cue. Re-arming + log_boot_feedback retransmit read
     * as two power-on lights after OTA (owner 2026-07-28). */

    diag_log_init();
    if (status_led_ready) {
        status_led_log_boot_feedback_after_diag_init();
    }
    esp_err_t ota_init_ret = firmware_ota_init();
    if (ota_init_ret != ESP_OK) {
        ESP_LOGE(TAG, "firmware_ota_init failed: %s; OTA disabled", esp_err_to_name(ota_init_ret));
    }
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
    default:                boot_reason = DIAG_BOOT_RESET; break;
    }
    diag_log(DIAG_SRC_SYSTEM, DIAG_SYS_BOOT, DIAG_SEV_INFO,
             boot_reason, 0, 0, 0);
    esp_err_t power_ret = power_manager_init();
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "power manager init failed: %s", esp_err_to_name(power_ret));
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_POWER, STATUS_LED_ERROR_HARD, "power_manager_init_failed");
        }
    }

    self_test_result_t post = self_test_run();
    if (!self_test_critical_ok(&post)) {
        ESP_LOGE(TAG, "POST failed; continuing in degraded mode so BLE can expose device status");
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, "post_failed");
        }
    }
    esp_err_t settings_ret = device_settings_init();
    if (settings_ret != ESP_OK) {
        ESP_LOGW(TAG, "device settings init skipped: %s", esp_err_to_name(settings_ret));
    }
    status_led_apply_device_settings();

    bool safe_mode = boot_safety_is_safe_mode();
    if (safe_mode) {
        ESP_LOGW(TAG, "boot safety safe mode active: BLE HID and recovery diagnostics only; audio disabled");
        ESP_LOGW(TAG, "device_status state=recovery detail=boot_safety_safe_mode");
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, "boot_safety_safe_mode");
        }
    }

    ble_hid_set_safe_mode(safe_mode);
    esp_err_t ble_ret = ble_hid_init();
    if (ble_ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE HID init degraded: %s", esp_err_to_name(ble_ret));
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_BLE, STATUS_LED_ERROR_HARD, "ble_hid_init_failed");
        }
    }
    esp_err_t keyboard_ret = safe_mode ? keyboard_start_safe_mode() : keyboard_start();
    if (keyboard_ret != ESP_OK) {
        ESP_LOGW(TAG, "keyboard start degraded; continuing BLE startup: %s", esp_err_to_name(keyboard_ret));
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_SYSTEM, STATUS_LED_ERROR_HARD, "keyboard_start_failed");
        }
    }
    system_health_init();
    esp_err_t ble_start_ret = ble_ret == ESP_OK ? ble_hid_start() : ble_ret;
    /*
     * Mic/AFE must start only after NimBLE host is running. Starting audio first
     * exhausted internal DRAM (~3KB free) and paniced ble_hs_event_start_stage2,
     * which then latched boot_safety safe_mode (red error LED).
     */
    if (!safe_mode) {
        esp_err_t audio_ret = voice_recording_control_enable_audio();
        if (audio_ret != ESP_OK) {
            ESP_LOGW(TAG, "deferred audio enable degraded: %s", esp_err_to_name(audio_ret));
        }
    }
    system_health_start();
    power_ret = power_manager_start();
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "power manager start failed: %s", esp_err_to_name(power_ret));
        if (status_led_ready) {
            status_led_set_error(STATUS_LED_ERROR_DOMAIN_POWER, STATUS_LED_ERROR_HARD, "power_manager_start_failed");
        }
    }
    bool ota_post_ok = self_test_critical_ok(&post);
    bool ota_ble_ready = ble_ret == ESP_OK && ble_start_ret == ESP_OK;
    bool ota_keyboard_ready = keyboard_ret == ESP_OK;
#ifdef LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL
    ESP_LOGE(TAG, "OTA rollback validation build: forcing pending-verify self-check failure");
    ota_post_ok = false;
#endif
    firmware_ota_record_self_check(ota_post_ok, ota_ble_ready, ota_keyboard_ready);
    /* 修复2：不再开机即用浅层 init 返回值确认 pending-verify（init OK 不等于链路真的可用）。
     * POST 失败 → 立即回滚（坏固件不应留存）；其余情况改由运行期事件驱动确认（首次观测到
     * 真实 BLE 安全连接）+ 60s 干净运行兜底确认，避免好固件因从未连接而长期卡 pending。 */
    if (!ota_post_ok) {
        firmware_ota_confirm_pending_verify_if_ready();
    }
    boot_safety_start_normal_boot_clear_timer();
}
