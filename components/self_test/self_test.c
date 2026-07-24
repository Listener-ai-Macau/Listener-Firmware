#include "self_test.h"
#include "self_test_platform.h"
#include "listener_device.h"
#include "denzic_device_health_v1.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "diag_log.h"

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
        .protocol_version = listener_device_get_protocol_version(),
    };

    result.nvs_ok = self_test_platform_check_nvs(&result.nvs_recovered, &result.nvs_error);
    result.spiram_required = self_test_platform_spiram_required();
    result.spiram_ok = self_test_platform_check_spiram();
    result.free_heap = self_test_platform_get_free_heap();
    diag_log(DIAG_SRC_SYSTEM, DIAG_SYS_INIT_RESULT,
             result.nvs_ok ? DIAG_SEV_INFO : DIAG_SEV_ERROR,
             DIAG_COMP_NVS,
             result.nvs_ok ? 0 : (uint32_t)result.nvs_error,
             result.nvs_recovered ? 1 : 0,
             0);
    diag_log(DIAG_SRC_SYSTEM, DIAG_SYS_INIT_RESULT,
             (result.spiram_ok || !result.spiram_required) ? DIAG_SEV_INFO : DIAG_SEV_ERROR,
             DIAG_COMP_SPIRAM,
             result.spiram_ok ? 0 : (uint32_t)ESP_ERR_NOT_FOUND,
             result.spiram_required ? 1 : 0,
             0);

    ESP_LOGI(TAG, "POST: nvs=%s nvs_err=%d ble=PENDING audio=PENDING spiram=%s heap=%lu fw=%s proto=%s",
             self_test_nvs_status(&result),
             result.nvs_error,
             self_test_spiram_status(&result),
             (unsigned long)result.free_heap,
             result.fw_version,
             result.protocol_version);
    diag_log(DIAG_SRC_SELF_TEST, DIAG_ST_POST_RESULT, result.nvs_ok ? DIAG_SEV_INFO : DIAG_SEV_ERROR,
             (uint32_t)result.nvs_ok, (uint32_t)result.spiram_ok,
             esp_get_free_heap_size() / 1024, self_test_critical_ok(&result) ? 1 : 0);
    if (!self_test_critical_ok(&result)) {
        ESP_LOGE(TAG, "POST critical checks failed: nvs=%s spiram=%s",
                 self_test_nvs_status(&result),
                 self_test_spiram_status(&result));
        diag_log(DIAG_SRC_SELF_TEST, DIAG_ST_POST_RESULT, DIAG_SEV_ERROR,
                 (uint32_t)result.nvs_ok, (uint32_t)result.spiram_ok,
                 esp_get_free_heap_size() / 1024, 0);
    }

    return result;
}

bool self_test_critical_ok(const self_test_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    const denzic_device_health_v1_check_t checks[] = {
        {
            .state = result->nvs_ok
                ? (result->nvs_recovered
                    ? DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_RECOVERED
                    : DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_PASSED)
                : DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_FAILED,
            .critical = true,
        },
        {
            .state = !result->spiram_required
                ? DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_NOT_REQUIRED
                : (result->spiram_ok
                    ? DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_PASSED
                    : DENZIC_DEVICE_HEALTH_V1_CHECK_STATE_FAILED),
            .critical = true,
        },
    };
    denzic_device_health_v1_self_test_summary_t summary =
        denzic_device_health_v1_summarize_checks(
            checks, sizeof(checks) / sizeof(checks[0]));
    return summary.ready;
}
