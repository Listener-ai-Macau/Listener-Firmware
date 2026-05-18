#include "listener_device.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "listener_device";

static char s_serial_str[18];
static char s_build_id_str[32];
static bool s_serial_initialized = false;

const char *listener_device_get_fw_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

const char *listener_device_get_build_id(void)
{
    if (s_build_id_str[0] == '\0') {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        snprintf(s_build_id_str, sizeof(s_build_id_str), "%s %s",
                 app_desc->date, app_desc->time);
    }
    return s_build_id_str;
}

const char *listener_device_get_serial(void)
{
    if (!s_serial_initialized) {
        uint8_t mac[6];
        esp_err_t ret = esp_efuse_mac_get_default(mac);
        if (ret == ESP_OK) {
            snprintf(s_serial_str, sizeof(s_serial_str),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            ESP_LOGW(TAG, "eFuse MAC read failed: %s", esp_err_to_name(ret));
            snprintf(s_serial_str, sizeof(s_serial_str), "unknown");
        }
        s_serial_initialized = true;
    }
    return s_serial_str;
}
