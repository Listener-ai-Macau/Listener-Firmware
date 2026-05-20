#include "keyboard.h"
#include "ble_hid.h"
#include "self_test.h"

#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    self_test_init();
    self_test_result_t post = self_test_run();
    if (!self_test_critical_ok(&post)) {
        ESP_LOGE(TAG, "POST failed; continuing in degraded mode so BLE can expose device status");
    }

    ble_hid_init();
    keyboard_start();
    ble_hid_start();
}
