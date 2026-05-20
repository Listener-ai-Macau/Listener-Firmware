#include "self_test_platform.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "self_test_plat";

void self_test_platform_init(void)
{
}

bool self_test_platform_check_nvs(bool *out_recovered, int *out_error)
{
    bool recovered = false;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires recovery: %s", esp_err_to_name(ret));
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(erase_ret));
            if (out_recovered != NULL) {
                *out_recovered = false;
            }
            if (out_error != NULL) {
                *out_error = erase_ret;
            }
            return false;
        }
        ret = nvs_flash_init();
        recovered = (ret == ESP_OK);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        if (out_recovered != NULL) {
            *out_recovered = recovered;
        }
        if (out_error != NULL) {
            *out_error = ret;
        }
        return false;
    }

    nvs_stats_t stats;
    ret = nvs_get_stats(NULL, &stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS stat failed: %s", esp_err_to_name(ret));
        if (out_recovered != NULL) {
            *out_recovered = recovered;
        }
        if (out_error != NULL) {
            *out_error = ret;
        }
        return false;
    }

    ESP_LOGI(
        TAG,
        "NVS stats: used=%u free=%u total=%u recovered=%s",
        (unsigned)stats.used_entries,
        (unsigned)stats.free_entries,
        (unsigned)stats.total_entries,
        recovered ? "yes" : "no");
    if (out_recovered != NULL) {
        *out_recovered = recovered;
    }
    if (out_error != NULL) {
        *out_error = ESP_OK;
    }
    return stats.total_entries > 0;
}

bool self_test_platform_check_spiram(void)
{
    size_t spiram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (spiram_size > 0) {
        ESP_LOGI(TAG, "SPIRAM: %u bytes", (unsigned)spiram_size);
        return true;
    }
    if (self_test_platform_spiram_required()) {
        ESP_LOGE(TAG, "SPIRAM configured but unavailable");
    } else {
        ESP_LOGI(TAG, "SPIRAM not configured");
    }
    return false;
}

bool self_test_platform_spiram_required(void)
{
#ifdef CONFIG_SPIRAM
    return true;
#else
    return false;
#endif
}

uint32_t self_test_platform_get_free_heap(void)
{
    return (uint32_t)esp_get_free_heap_size();
}
