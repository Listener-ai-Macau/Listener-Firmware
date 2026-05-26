#include "keyboard.h"
#include "ble_hid.h"
#include "self_test.h"
#include "system_health.h"
#include "diag_log.h"
#include "firmware_ota.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "app_main";

void app_main(void)
{
    self_test_init();
    diag_log_init();
    firmware_ota_init();

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

    self_test_result_t post = self_test_run();
    if (!self_test_critical_ok(&post)) {
        ESP_LOGE(TAG, "POST failed; continuing in degraded mode so BLE can expose device status");
    }

    esp_err_t ble_ret = ble_hid_init();
    if (ble_ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE HID init degraded: %s", esp_err_to_name(ble_ret));
    }
    esp_err_t keyboard_ret = keyboard_start();
    if (keyboard_ret != ESP_OK) {
        ESP_LOGW(TAG, "keyboard start degraded; continuing BLE startup: %s", esp_err_to_name(keyboard_ret));
    }
    system_health_init();
    esp_err_t ble_start_ret = ble_ret == ESP_OK ? ble_hid_start() : ble_ret;
    system_health_start();
    firmware_ota_record_self_check(
        self_test_critical_ok(&post),
        ble_ret == ESP_OK && ble_start_ret == ESP_OK,
        keyboard_ret == ESP_OK);
    firmware_ota_confirm_pending_verify_if_ready();
}
