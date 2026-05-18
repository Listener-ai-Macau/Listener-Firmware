#include "self_test.h"
#include "self_test_platform.h"
#include "listener_device.h"

#include "esp_log.h"

#define LISTENER_PROTOCOL_VERSION_STR "1"

static const char *TAG = "self_test";

void self_test_init(void)
{
    self_test_platform_init();
}

self_test_result_t self_test_run(void)
{
    self_test_result_t result = {
        .nvs_ok = false,
        .ble_ok = false,
        .audio_codec_ok = false,
        .spiram_ok = false,
        .free_heap = 0,
        .fw_version = listener_device_get_fw_version(),
        .protocol_version = LISTENER_PROTOCOL_VERSION_STR,
    };

    result.nvs_ok = self_test_platform_check_nvs();
    result.free_heap = self_test_platform_get_free_heap();

    ESP_LOGI(TAG, "POST: nvs=%s ble=PENDING audio=PENDING spiram=%s heap=%lu fw=%s proto=%s",
             result.nvs_ok ? "OK" : "FAIL",
             self_test_platform_check_spiram() ? "OK" : "N/A",
             (unsigned long)result.free_heap,
             result.fw_version,
             result.protocol_version);

    return result;
}
