#include "self_test_platform.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "self_test_plat";

void self_test_platform_init(void)
{
}

bool self_test_platform_check_nvs(void)
{
    nvs_stats_t stats;
    esp_err_t ret = nvs_get_stats(NULL, &stats);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS stat failed: %s", esp_err_to_name(ret));
        return false;
    }
    return stats.used_entries > 0 || stats.total_entries > 0;
}

bool self_test_platform_check_spiram(void)
{
    size_t spiram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (spiram_size > 0) {
        ESP_LOGI(TAG, "SPIRAM: %u bytes", (unsigned)spiram_size);
        return true;
    }
    return false;
}

uint32_t self_test_platform_get_free_heap(void)
{
    return (uint32_t)esp_get_free_heap_size();
}
