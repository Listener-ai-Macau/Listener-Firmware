#include "self_test.h"
#include "self_test_platform.h"
#include "listener_device.h"

#include "esp_log.h"

#define LISTENER_PROTOCOL_VERSION_STR "1"

static const char *TAG = "self_test";

static const char *self_test_nvs_status(const self_test_result_t *result)
{
    if (result->nvs_ok && result->nvs_recovered) {
        return "RECOVERED";
    }
    return result->nvs_ok ? "OK" : "FAIL";
}

static const char *self_test_spiram_status(const self_test_result_t *result)
{
    if (result->spiram_ok) {
        return "OK";
    }
    return result->spiram_required ? "FAIL" : "N/A";
}

void self_test_init(void)
{
    self_test_platform_init();
}

self_test_result_t self_test_run(void)
{
    self_test_result_t result = {
        .nvs_ok = false,
        .nvs_recovered = false,
        .ble_ok = false,
        .audio_codec_ok = false,
        .spiram_ok = false,
        .spiram_required = false,
        .nvs_error = 0,
        .free_heap = 0,
        .fw_version = listener_device_get_fw_version(),
        .protocol_version = LISTENER_PROTOCOL_VERSION_STR,
    };

    result.nvs_ok = self_test_platform_check_nvs(&result.nvs_recovered, &result.nvs_error);
    result.spiram_required = self_test_platform_spiram_required();
    result.spiram_ok = self_test_platform_check_spiram();
    result.free_heap = self_test_platform_get_free_heap();

    ESP_LOGI(TAG, "POST: nvs=%s nvs_err=%d ble=PENDING audio=PENDING spiram=%s heap=%lu fw=%s proto=%s",
             self_test_nvs_status(&result),
             result.nvs_error,
             self_test_spiram_status(&result),
             (unsigned long)result.free_heap,
             result.fw_version,
             result.protocol_version);
    if (!self_test_critical_ok(&result)) {
        ESP_LOGE(TAG, "POST critical checks failed: nvs=%s spiram=%s",
                 self_test_nvs_status(&result),
                 self_test_spiram_status(&result));
    }

    return result;
}

bool self_test_critical_ok(const self_test_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (!result->nvs_ok) {
        return false;
    }

    return !result->spiram_required || result->spiram_ok;
}
