#include "keyboard.h"
#include "ble_hid.h"
#include "self_test.h"
#include "system_health.h"
#include "diag_log.h"

#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "app_main";

void app_main(void)
{
    self_test_init();
    diag_log_init();

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

    ble_hid_init();
    keyboard_start();
    system_health_init();
    ble_hid_start();
    system_health_start();
}
