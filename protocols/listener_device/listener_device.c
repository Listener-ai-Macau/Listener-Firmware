#include "listener_device.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "device_settings.h"
#include "power_manager.h"

static const char *TAG = "listener_device";

static char s_serial_str[18];
static char s_build_id_str[32];
static char s_software_revision_str[48];
static char s_readiness_str[320];
static char s_capabilities_str[224];
static bool s_serial_initialized = false;
static bool s_safe_mode;
static uint32_t s_ready_mask;
static uint32_t s_degraded_mask;

static void listener_device_append_token(char *buffer, size_t buffer_size, const char *token)
{
    if (buffer == NULL || buffer_size == 0 || token == NULL || token[0] == '\0') {
        return;
    }

    size_t used = strlen(buffer);
    snprintf(
        buffer + used,
        buffer_size - used,
        "%s%s",
        used == 0 ? "" : ";",
        token);
}

static void listener_device_append_subsystem_tokens(
    char *buffer,
    size_t buffer_size,
    uint32_t mask,
    const char *suffix)
{
    struct {
        uint32_t bit;
        const char *name;
    } entries[] = {
        {LISTENER_DEVICE_READY_HID, "hid"},
        {LISTENER_DEVICE_READY_AUDIO, "audio"},
        {LISTENER_DEVICE_READY_OTA, "ota"},
        {LISTENER_DEVICE_READY_DIAGNOSTIC, "diagnostic"},
    };

    for (size_t index = 0; index < sizeof(entries) / sizeof(entries[0]); ++index) {
        if ((mask & entries[index].bit) == 0) {
            continue;
        }

        char token[32];
        snprintf(token, sizeof(token), "%s_%s", entries[index].name, suffix);
        listener_device_append_token(buffer, buffer_size, token);
    }
}

static void listener_device_append_bool_token(
    char *buffer,
    size_t buffer_size,
    const char *key,
    bool value)
{
    char token[32];
    snprintf(token, sizeof(token), "%s=%u", key, value ? 1u : 0u);
    listener_device_append_token(buffer, buffer_size, token);
}

static void listener_device_append_u8_token(
    char *buffer,
    size_t buffer_size,
    const char *key,
    uint8_t value)
{
    char token[32];
    snprintf(token, sizeof(token), "%s=%u", key, (unsigned)value);
    listener_device_append_token(buffer, buffer_size, token);
}

const char *listener_device_get_fw_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

const char *listener_device_get_build_id(void)
{
    if (s_build_id_str[0] == '\0') {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        /*
         * app_elf_sha256 is stable for the exact linked image, unlike the old
         * compile date/time string. Expose a compact prefix so Type can prove
         * which binary is running after notify becomes ready.
         */
        for (size_t index = 0; index < 8; ++index) {
            snprintf(
                s_build_id_str + (index * 2),
                sizeof(s_build_id_str) - (index * 2),
                "%02x",
                app_desc->app_elf_sha256[index]);
        }
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

const char *listener_device_get_ble_name(void)
{
    return device_settings_get_ble_name();
}

const char *listener_device_get_protocol_version(void)
{
    return LISTENER_PROTOCOL_VERSION_STR;
}

const char *listener_device_get_software_revision(void)
{
    if (s_software_revision_str[0] == '\0') {
        snprintf(
            s_software_revision_str,
            sizeof(s_software_revision_str),
            "protocol=%s;build_id=%s",
            listener_device_get_protocol_version(),
            listener_device_get_build_id());
    }
    return s_software_revision_str;
}

const char *listener_device_get_factory_readiness(void)
{
    s_readiness_str[0] = '\0';
    power_manager_snapshot_t power = {0};
    power_manager_get_snapshot(&power);
    listener_device_append_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        LISTENER_DEVICE_FACTORY_READINESS);
    char model_token[32];
    snprintf(model_token, sizeof(model_token), "model=%s", LISTENER_DEVICE_MODEL);
    listener_device_append_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        model_token);
    char fw_token[64];
    snprintf(fw_token, sizeof(fw_token), "fw_version=%s", listener_device_get_fw_version());
    listener_device_append_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        fw_token);
    char build_token[40];
    snprintf(build_token, sizeof(build_token), "build_id=%s", listener_device_get_build_id());
    listener_device_append_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        build_token);
    listener_device_append_subsystem_tokens(
        s_readiness_str,
        sizeof(s_readiness_str),
        s_ready_mask,
        "ready");
    listener_device_append_subsystem_tokens(
        s_readiness_str,
        sizeof(s_readiness_str),
        s_degraded_mask,
        "degraded");
    if (s_safe_mode) {
        listener_device_append_token(
            s_readiness_str,
            sizeof(s_readiness_str),
            "boot_safety_safe_mode");
    }
    listener_device_append_bool_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "external_power_present",
        power.external_power_present);
    listener_device_append_bool_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "usb_power_present",
        power.usb_power_present);
    listener_device_append_bool_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "charging",
        power.charging);
    listener_device_append_bool_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "charge_full",
        power.charge_full);
    listener_device_append_bool_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "battery_valid",
        power.battery_valid);
    listener_device_append_u8_token(
        s_readiness_str,
        sizeof(s_readiness_str),
        "battery_level",
        power.battery_valid ? power.battery_level_percent : 0xFF);
    return s_readiness_str;
}

const char *listener_device_get_capabilities(void)
{
    s_capabilities_str[0] = '\0';
    if (s_safe_mode) {
        listener_device_append_token(
            s_capabilities_str,
            sizeof(s_capabilities_str),
            "ble_hid_keyboard;usb_serial_text;post_status;denzic_ota_v1;boot_safety_safe_mode;audio_disabled");
    } else {
        listener_device_append_token(
            s_capabilities_str,
            sizeof(s_capabilities_str),
            LISTENER_DEVICE_CAPABILITIES);
    }

    listener_device_append_subsystem_tokens(
        s_capabilities_str,
        sizeof(s_capabilities_str),
        s_ready_mask,
        "ready");
    listener_device_append_subsystem_tokens(
        s_capabilities_str,
        sizeof(s_capabilities_str),
        s_degraded_mask,
        "degraded");
    return s_capabilities_str;
}

void listener_device_set_safe_mode(bool enabled)
{
    s_safe_mode = enabled;
}

void listener_device_set_readiness(uint32_t ready_mask, uint32_t degraded_mask)
{
    s_ready_mask = ready_mask;
    s_degraded_mask = degraded_mask;
}

uint32_t listener_device_get_ready_mask(void)
{
    return s_ready_mask;
}

uint32_t listener_device_get_degraded_mask(void)
{
    return s_degraded_mask;
}
