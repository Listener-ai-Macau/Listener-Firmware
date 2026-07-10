#include "device_settings.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "ec11_rotation_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"

#ifndef CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS
#define CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS 600000
#endif

#define DEVICE_SETTINGS_NVS_NAMESPACE "device"
#define DEVICE_SETTINGS_NVS_PLUGGED_BRIGHTNESS_KEY "plug_brt"
#define DEVICE_SETTINGS_NVS_BATTERY_BRIGHTNESS_KEY "bat_brt"
#define DEVICE_SETTINGS_NVS_STATUS_LED_BRIGHTNESS_KEY "led_st"
#define DEVICE_SETTINGS_NVS_KEY_LED_BRIGHTNESS_KEY "led_key"
#define DEVICE_SETTINGS_NVS_EC11_LED_BRIGHTNESS_KEY "led_ec11"
#define DEVICE_SETTINGS_NVS_EDGE_LED_BRIGHTNESS_KEY "led_edge"
#define DEVICE_SETTINGS_NVS_LOW_POWER_IDLE_MS_KEY "lp_ms"
#define DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_IDLE_MS_KEY "plug_lp_ms"
#define DEVICE_SETTINGS_NVS_BATTERY_LOW_POWER_IDLE_MS_KEY "bat_lp_ms"
#define DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_KEY "plug_lp"
#define DEVICE_SETTINGS_NVS_AUTO_SHUTDOWN_MS_KEY "shut_ms"
#define DEVICE_SETTINGS_NVS_PLUGGED_AUTO_SHUTDOWN_MS_KEY "plug_shut"
#define DEVICE_SETTINGS_NVS_BLE_NAME_KEY "ble_name"
#define DEVICE_SETTINGS_NVS_KNOB_ROTATION_KEY "knob_rot"
#define DEVICE_SETTINGS_USB_PREFIX "DEVICE:"
#define DEVICE_SETTINGS_COMMAND_BUFFER_BYTES 192
#define DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS 0U
#define DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS 60000U
#define DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS 86400000U
#define DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS 60000U
#define DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS 86400000U
#define DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS 0U

static const char *TAG = "device_settings";

typedef struct {
    uint8_t plugged_brightness_percent;
    uint8_t battery_brightness_percent;
    uint8_t status_led_brightness_percent;
    uint8_t key_led_brightness_percent;
    uint8_t ec11_led_brightness_percent;
    uint8_t edge_led_brightness_percent;
    uint32_t low_power_idle_ms;
    uint32_t plugged_low_power_idle_ms;
    uint32_t battery_low_power_idle_ms;
    bool plugged_low_power_enabled;
    uint32_t plugged_auto_shutdown_ms;
    uint32_t battery_auto_shutdown_ms;
    uint8_t knob_rotation_action;
    char ble_name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1];
} device_settings_config_t;

static SemaphoreHandle_t s_mutex;
static device_settings_config_t s_settings;
static bool s_loaded;
static bool s_loaded_from_nvs;
static bool s_ble_name_pending_restart;

static esp_err_t device_settings_persist_locked(bool loaded_from_nvs_after_persist);

static void device_settings_note_nvs_read_locked(esp_err_t ret, bool *missing_saved_key)
{
    if (ret == ESP_OK) {
        s_loaded_from_nvs = true;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND && missing_saved_key != NULL) {
        *missing_saved_key = true;
    }
}

static void device_settings_set_defaults_locked(void)
{
    s_settings.plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
    s_settings.battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
    s_settings.status_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT;
    s_settings.key_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT;
    s_settings.ec11_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    s_settings.edge_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    s_settings.low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS;
    s_settings.plugged_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_IDLE_MS;
    s_settings.battery_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_BATTERY_LOW_POWER_IDLE_MS;
    s_settings.plugged_low_power_enabled = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED != 0;
    s_settings.plugged_auto_shutdown_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS;
    s_settings.battery_auto_shutdown_ms = (uint32_t)CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS;
    s_settings.knob_rotation_action = (uint8_t)EC11_ROTATION_ACTION_SYSTEM_VOLUME;
    snprintf(s_settings.ble_name, sizeof(s_settings.ble_name), "%s", DEVICE_SETTINGS_DEFAULT_BLE_NAME);
}

static ec11_rotation_action_t device_settings_knob_rotation_action_locked(void)
{
    switch ((ec11_rotation_action_t)s_settings.knob_rotation_action) {
    case EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS:
        return EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS;
    case EC11_ROTATION_ACTION_DISABLED:
        return EC11_ROTATION_ACTION_DISABLED;
    case EC11_ROTATION_ACTION_SYSTEM_VOLUME:
    default:
        return EC11_ROTATION_ACTION_SYSTEM_VOLUME;
    }
}

static void device_settings_apply_runtime_locked(const char *source)
{
    (void)ec11_rotation_control_set_action(
        device_settings_knob_rotation_action_locked(),
        source != NULL ? source : "device_settings");
}

static bool device_settings_ensure_mutex(void)
{
    if (s_mutex != NULL) {
        return true;
    }
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex != NULL;
}

static uint8_t device_settings_clamp_brightness(uint8_t value)
{
    return value <= 100U ? value : 100U;
}

static uint32_t device_settings_clamp_auto_shutdown_ms(uint32_t value)
{
    if (value == DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS) {
        return DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS;
    }
    if (value < DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS) {
        return DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS;
    }
    if (value > DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS) {
        return DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS;
    }
    return value;
}

static uint32_t device_settings_clamp_low_power_idle_ms(uint32_t value)
{
    if (value == DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS) {
        return DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS;
    }
    if (value < DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS) {
        return DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS;
    }
    if (value > DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS) {
        return DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS;
    }
    return value;
}

static bool device_settings_validate_ble_name(const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len == 0 || len > DEVICE_SETTINGS_BLE_NAME_MAX_LEN) {
        return false;
    }
    for (size_t index = 0; index < len; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 0x20 || ch > 0x7e) {
            return false;
        }
        if (ch == '"' || ch == '\'' || ch == ';' || ch == '=' || ch == '\\') {
            return false;
        }
    }
    return true;
}

static esp_err_t device_settings_load_locked(void)
{
    device_settings_set_defaults_locked();
    s_loaded_from_nvs = false;

    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(DEVICE_SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = device_settings_persist_locked(false);
        if (ret != ESP_OK) {
            return ret;
        }
        ESP_LOGI(TAG, "device settings NVS namespace missing; persisted product defaults");
        s_loaded = true;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    bool missing_saved_key = false;

    uint8_t plugged = s_settings.plugged_brightness_percent;
    esp_err_t get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_PLUGGED_BRIGHTNESS_KEY, &plugged);
    if (get_ret == ESP_OK) {
        s_settings.plugged_brightness_percent = device_settings_clamp_brightness(plugged);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint8_t battery = s_settings.battery_brightness_percent;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_BATTERY_BRIGHTNESS_KEY, &battery);
    if (get_ret == ESP_OK) {
        s_settings.battery_brightness_percent = device_settings_clamp_brightness(battery);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);
    s_settings.battery_brightness_percent = s_settings.plugged_brightness_percent;
    if (s_settings.plugged_brightness_percent != DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT ||
        s_settings.battery_brightness_percent != DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT) {
        ESP_LOGW(TAG, "ignoring legacy global brightness cap plugged=%u battery=%u; per-zone brightness is authoritative",
                 s_settings.plugged_brightness_percent,
                 s_settings.battery_brightness_percent);
        s_settings.plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
        s_settings.battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
    }

    uint8_t status_led = s_settings.status_led_brightness_percent;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_STATUS_LED_BRIGHTNESS_KEY, &status_led);
    if (get_ret == ESP_OK) {
        s_settings.status_led_brightness_percent = device_settings_clamp_brightness(status_led);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint8_t key_led = s_settings.key_led_brightness_percent;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_KEY_LED_BRIGHTNESS_KEY, &key_led);
    if (get_ret == ESP_OK) {
        s_settings.key_led_brightness_percent = device_settings_clamp_brightness(key_led);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint8_t ec11_led = s_settings.ec11_led_brightness_percent;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_EC11_LED_BRIGHTNESS_KEY, &ec11_led);
    if (get_ret == ESP_OK) {
        s_settings.ec11_led_brightness_percent = device_settings_clamp_brightness(ec11_led);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint8_t edge_led = s_settings.edge_led_brightness_percent;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_EDGE_LED_BRIGHTNESS_KEY, &edge_led);
    if (get_ret == ESP_OK) {
        s_settings.edge_led_brightness_percent = device_settings_clamp_brightness(edge_led);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint32_t low_power_idle_ms = s_settings.low_power_idle_ms;
    get_ret = nvs_get_u32(nvs, DEVICE_SETTINGS_NVS_LOW_POWER_IDLE_MS_KEY, &low_power_idle_ms);
    if (get_ret == ESP_OK) {
        s_settings.low_power_idle_ms = device_settings_clamp_low_power_idle_ms(low_power_idle_ms);
        s_settings.plugged_low_power_idle_ms = s_settings.low_power_idle_ms;
        s_settings.battery_low_power_idle_ms = s_settings.low_power_idle_ms;
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint32_t plugged_low_power_idle_ms = s_settings.plugged_low_power_idle_ms;
    get_ret = nvs_get_u32(nvs, DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_IDLE_MS_KEY, &plugged_low_power_idle_ms);
    if (get_ret == ESP_OK) {
        s_settings.plugged_low_power_idle_ms = device_settings_clamp_low_power_idle_ms(plugged_low_power_idle_ms);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint32_t battery_low_power_idle_ms = s_settings.battery_low_power_idle_ms;
    get_ret = nvs_get_u32(nvs, DEVICE_SETTINGS_NVS_BATTERY_LOW_POWER_IDLE_MS_KEY, &battery_low_power_idle_ms);
    if (get_ret == ESP_OK) {
        s_settings.battery_low_power_idle_ms = device_settings_clamp_low_power_idle_ms(battery_low_power_idle_ms);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    s_settings.low_power_idle_ms = s_settings.battery_low_power_idle_ms;

    uint8_t plugged_low_power = s_settings.plugged_low_power_enabled ? 1U : 0U;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_KEY, &plugged_low_power);
    if (get_ret == ESP_OK) {
        s_settings.plugged_low_power_enabled = plugged_low_power != 0U;
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint32_t plugged_shutdown_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS;
    get_ret = nvs_get_u32(nvs, DEVICE_SETTINGS_NVS_PLUGGED_AUTO_SHUTDOWN_MS_KEY, &plugged_shutdown_ms);
    if (get_ret == ESP_OK) {
        if (plugged_shutdown_ms != DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS) {
            ESP_LOGW(TAG, "ignoring legacy plugged_auto_shutdown_ms=%" PRIu32 "; external power auto-shutdown is disabled", plugged_shutdown_ms);
        }
        s_settings.plugged_auto_shutdown_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS;
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint32_t shutdown_ms = s_settings.battery_auto_shutdown_ms;
    get_ret = nvs_get_u32(nvs, DEVICE_SETTINGS_NVS_AUTO_SHUTDOWN_MS_KEY, &shutdown_ms);
    if (get_ret == ESP_OK) {
        s_settings.battery_auto_shutdown_ms = device_settings_clamp_auto_shutdown_ms(shutdown_ms);
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    uint8_t knob_rotation = s_settings.knob_rotation_action;
    get_ret = nvs_get_u8(nvs, DEVICE_SETTINGS_NVS_KNOB_ROTATION_KEY, &knob_rotation);
    if (get_ret == ESP_OK) {
        if (knob_rotation <= (uint8_t)EC11_ROTATION_ACTION_DISABLED) {
            s_settings.knob_rotation_action = knob_rotation;
        }
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    char name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1] = {0};
    size_t name_len = sizeof(name);
    get_ret = nvs_get_str(nvs, DEVICE_SETTINGS_NVS_BLE_NAME_KEY, name, &name_len);
    if (get_ret == ESP_OK) {
        if (device_settings_validate_ble_name(name)) {
            snprintf(s_settings.ble_name, sizeof(s_settings.ble_name), "%s", name);
        }
    }
    device_settings_note_nvs_read_locked(get_ret, &missing_saved_key);

    bool loaded_from_nvs_after_read = s_loaded_from_nvs;
    nvs_close(nvs);
    if (missing_saved_key || !loaded_from_nvs_after_read) {
        ret = device_settings_persist_locked(loaded_from_nvs_after_read);
        if (ret != ESP_OK) {
            return ret;
        }
        ESP_LOGI(
            TAG,
            "device settings filled missing NVS keys with product defaults loaded_from_nvs=%u",
            loaded_from_nvs_after_read ? 1u : 0u);
    }
    s_loaded = true;
    return ESP_OK;
}

static esp_err_t device_settings_ensure_loaded_locked(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    return device_settings_load_locked();
}

static esp_err_t device_settings_persist_locked(bool loaded_from_nvs_after_persist)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(DEVICE_SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(nvs, DEVICE_SETTINGS_NVS_PLUGGED_BRIGHTNESS_KEY, s_settings.plugged_brightness_percent);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs, DEVICE_SETTINGS_NVS_BATTERY_BRIGHTNESS_KEY, s_settings.battery_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(
            nvs,
            DEVICE_SETTINGS_NVS_STATUS_LED_BRIGHTNESS_KEY,
            s_settings.status_led_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(
            nvs,
            DEVICE_SETTINGS_NVS_KEY_LED_BRIGHTNESS_KEY,
            s_settings.key_led_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(
            nvs,
            DEVICE_SETTINGS_NVS_EC11_LED_BRIGHTNESS_KEY,
            s_settings.ec11_led_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(
            nvs,
            DEVICE_SETTINGS_NVS_EDGE_LED_BRIGHTNESS_KEY,
            s_settings.edge_led_brightness_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(nvs, DEVICE_SETTINGS_NVS_LOW_POWER_IDLE_MS_KEY, s_settings.battery_low_power_idle_ms);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(
            nvs,
            DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_IDLE_MS_KEY,
            s_settings.plugged_low_power_idle_ms);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(
            nvs,
            DEVICE_SETTINGS_NVS_BATTERY_LOW_POWER_IDLE_MS_KEY,
            s_settings.battery_low_power_idle_ms);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(
            nvs,
            DEVICE_SETTINGS_NVS_PLUGGED_LOW_POWER_KEY,
            s_settings.plugged_low_power_enabled ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(
            nvs,
            DEVICE_SETTINGS_NVS_PLUGGED_AUTO_SHUTDOWN_MS_KEY,
            DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(nvs, DEVICE_SETTINGS_NVS_AUTO_SHUTDOWN_MS_KEY, s_settings.battery_auto_shutdown_ms);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, DEVICE_SETTINGS_NVS_BLE_NAME_KEY, s_settings.ble_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs, DEVICE_SETTINGS_NVS_KNOB_ROTATION_KEY, s_settings.knob_rotation_action);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret == ESP_OK) {
        s_loaded_from_nvs = loaded_from_nvs_after_persist;
    }
    return ret;
}

esp_err_t device_settings_init(void)
{
    if (!device_settings_ensure_mutex()) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;
    uint8_t plugged_brightness = 100U;
    uint8_t battery_brightness = 100U;
    uint8_t status_led_brightness = DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT;
    uint8_t key_led_brightness = DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT;
    uint8_t ec11_led_brightness = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    uint8_t edge_led_brightness = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    uint32_t plugged_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_IDLE_MS;
    uint32_t battery_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_BATTERY_LOW_POWER_IDLE_MS;
    bool plugged_low_power_enabled = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED != 0;
    uint32_t plugged_auto_shutdown_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS;
    uint32_t battery_auto_shutdown_ms = (uint32_t)CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS;
    ec11_rotation_action_t knob_rotation_action = EC11_ROTATION_ACTION_SYSTEM_VOLUME;
    char ble_name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1];
    snprintf(ble_name, sizeof(ble_name), "%s", DEVICE_SETTINGS_DEFAULT_BLE_NAME);
    bool loaded_from_nvs = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_loaded = false;
        ret = device_settings_load_locked();
        if (ret == ESP_OK) {
            plugged_brightness = s_settings.plugged_brightness_percent;
            battery_brightness = s_settings.battery_brightness_percent;
            status_led_brightness = s_settings.status_led_brightness_percent;
            key_led_brightness = s_settings.key_led_brightness_percent;
            ec11_led_brightness = s_settings.ec11_led_brightness_percent;
            edge_led_brightness = s_settings.edge_led_brightness_percent;
            plugged_low_power_idle_ms = s_settings.plugged_low_power_idle_ms;
            battery_low_power_idle_ms = s_settings.battery_low_power_idle_ms;
            plugged_low_power_enabled = s_settings.plugged_low_power_enabled;
            plugged_auto_shutdown_ms = s_settings.plugged_auto_shutdown_ms;
            battery_auto_shutdown_ms = s_settings.battery_auto_shutdown_ms;
            knob_rotation_action = device_settings_knob_rotation_action_locked();
            snprintf(ble_name, sizeof(ble_name), "%s", s_settings.ble_name);
            loaded_from_nvs = s_loaded_from_nvs;
        }
        xSemaphoreGive(s_mutex);
    }

    if (ret == ESP_OK) {
        (void)ec11_rotation_control_set_action(knob_rotation_action, "device_settings_init");
        ESP_LOGI(
            TAG,
            "device settings: legacy_plugged_brightness=%u legacy_battery_brightness=%u plugged_low_power_idle_ms=%" PRIu32
            " battery_low_power_idle_ms=%" PRIu32 " plugged_low_power_enabled=%u"
            " led_status=%u led_key=%u led_ec11=%u led_edge=%u"
            " plugged_auto_shutdown_ms=%" PRIu32 " battery_auto_shutdown_ms=%" PRIu32
            " knob_rotation=%s ble_name=%s loaded_from_nvs=%u",
            plugged_brightness,
            battery_brightness,
            plugged_low_power_idle_ms,
            battery_low_power_idle_ms,
            plugged_low_power_enabled ? 1u : 0u,
            status_led_brightness,
            key_led_brightness,
            ec11_led_brightness,
            edge_led_brightness,
            plugged_auto_shutdown_ms,
            battery_auto_shutdown_ms,
            ec11_rotation_control_action_name(knob_rotation_action),
            ble_name,
            loaded_from_nvs ? 1u : 0u);
    } else {
        ESP_LOGW(TAG, "device settings load skipped: %s", esp_err_to_name(ret));
    }
    return ret;
}

void device_settings_get_snapshot(device_settings_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
    out_snapshot->battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
    out_snapshot->status_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_STATUS_LED_BRIGHTNESS_PERCENT;
    out_snapshot->key_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_KEY_LED_BRIGHTNESS_PERCENT;
    out_snapshot->ec11_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    out_snapshot->edge_led_brightness_percent = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    out_snapshot->low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_LOW_POWER_IDLE_MS;
    out_snapshot->plugged_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_IDLE_MS;
    out_snapshot->battery_low_power_idle_ms = DEVICE_SETTINGS_DEFAULT_BATTERY_LOW_POWER_IDLE_MS;
    out_snapshot->plugged_low_power_enabled = DEVICE_SETTINGS_DEFAULT_PLUGGED_LOW_POWER_ENABLED != 0;
    out_snapshot->plugged_auto_shutdown_ms = DEVICE_SETTINGS_DEFAULT_PLUGGED_AUTO_SHUTDOWN_MS;
    out_snapshot->battery_auto_shutdown_ms = (uint32_t)CONFIG_POWER_MANAGER_HARDWARE_SHUTDOWN_MS;
    snprintf(out_snapshot->ble_name, sizeof(out_snapshot->ble_name), "%s", DEVICE_SETTINGS_DEFAULT_BLE_NAME);

    if (!device_settings_ensure_mutex()) {
        return;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = device_settings_ensure_loaded_locked();
        if (ret == ESP_OK) {
            out_snapshot->plugged_brightness_percent = s_settings.plugged_brightness_percent;
            out_snapshot->battery_brightness_percent = s_settings.battery_brightness_percent;
            out_snapshot->status_led_brightness_percent = s_settings.status_led_brightness_percent;
            out_snapshot->key_led_brightness_percent = s_settings.key_led_brightness_percent;
            out_snapshot->ec11_led_brightness_percent = s_settings.ec11_led_brightness_percent;
            out_snapshot->edge_led_brightness_percent = s_settings.edge_led_brightness_percent;
            out_snapshot->low_power_idle_ms = s_settings.battery_low_power_idle_ms;
            out_snapshot->plugged_low_power_idle_ms = s_settings.plugged_low_power_idle_ms;
            out_snapshot->battery_low_power_idle_ms = s_settings.battery_low_power_idle_ms;
            out_snapshot->plugged_low_power_enabled = s_settings.plugged_low_power_enabled;
            out_snapshot->plugged_auto_shutdown_ms = s_settings.plugged_auto_shutdown_ms;
            out_snapshot->battery_auto_shutdown_ms = s_settings.battery_auto_shutdown_ms;
            snprintf(out_snapshot->ble_name, sizeof(out_snapshot->ble_name), "%s", s_settings.ble_name);
            out_snapshot->ble_name_pending_restart = s_ble_name_pending_restart;
            out_snapshot->loaded_from_nvs = s_loaded_from_nvs;
        }
        xSemaphoreGive(s_mutex);
    }
}

uint8_t device_settings_get_active_brightness_percent(bool external_power_present)
{
    (void)external_power_present;
    return DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
}

uint32_t device_settings_get_low_power_idle_ms(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.battery_low_power_idle_ms;
}

uint32_t device_settings_get_active_low_power_idle_ms(bool external_power_present)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return external_power_present
        ? snapshot.plugged_low_power_idle_ms
        : snapshot.battery_low_power_idle_ms;
}

uint32_t device_settings_get_plugged_low_power_idle_ms(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.plugged_low_power_idle_ms;
}

uint32_t device_settings_get_battery_low_power_idle_ms(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.battery_low_power_idle_ms;
}

bool device_settings_get_plugged_low_power_enabled(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.plugged_low_power_enabled;
}

uint32_t device_settings_get_active_auto_shutdown_ms(bool external_power_present)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return external_power_present
        ? snapshot.plugged_auto_shutdown_ms
        : snapshot.battery_auto_shutdown_ms;
}

uint32_t device_settings_get_plugged_auto_shutdown_ms(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.plugged_auto_shutdown_ms;
}

uint32_t device_settings_get_battery_auto_shutdown_ms(void)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    return snapshot.battery_auto_shutdown_ms;
}

const char *device_settings_get_ble_name(void)
{
    if (!device_settings_ensure_mutex()) {
        return DEVICE_SETTINGS_DEFAULT_BLE_NAME;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        (void)device_settings_ensure_loaded_locked();
        xSemaphoreGive(s_mutex);
    }
    return s_settings.ble_name[0] != '\0' ? s_settings.ble_name : DEVICE_SETTINGS_DEFAULT_BLE_NAME;
}

bool device_settings_ble_name_pending_restart(void)
{
    bool pending = false;
    if (!device_settings_ensure_mutex()) {
        return false;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        pending = s_ble_name_pending_restart;
        xSemaphoreGive(s_mutex);
    }
    return pending;
}

void device_settings_mark_ble_name_applied(void)
{
    if (!device_settings_ensure_mutex()) {
        return;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_ble_name_pending_restart = false;
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t device_settings_set_brightness_profiles(uint8_t plugged_percent, uint8_t battery_percent)
{
    if (!device_settings_ensure_mutex()) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        (void)device_settings_ensure_loaded_locked();
        (void)battery_percent;
        uint8_t zone_brightness = device_settings_clamp_brightness(plugged_percent);
        s_settings.plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
        s_settings.battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
        s_settings.status_led_brightness_percent = zone_brightness;
        s_settings.key_led_brightness_percent = zone_brightness;
        s_settings.ec11_led_brightness_percent = zone_brightness;
        s_settings.edge_led_brightness_percent = zone_brightness;
        ret = device_settings_persist_locked(true);
        xSemaphoreGive(s_mutex);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "legacy all-zone brightness persist failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static const char *device_settings_strip_prefix(const char *line)
{
    if (line == NULL) {
        return NULL;
    }
    if (*line == '~') {
        line++;
    }
    size_t prefix_len = strlen(DEVICE_SETTINGS_USB_PREFIX);
    if (strncmp(line, DEVICE_SETTINGS_USB_PREFIX, prefix_len) != 0) {
        return NULL;
    }
    return line + prefix_len;
}

static void device_settings_print_error(const char *key, const char *reason)
{
    printf("~DEVICE:ERROR key=%s reason=%s\n",
           key != NULL ? key : "command",
           reason != NULL ? reason : "invalid");
    fflush(stdout);
}

static void device_settings_print_status(const char *result)
{
    device_settings_snapshot_t snapshot = {0};
    device_settings_get_snapshot(&snapshot);
    board_v2_power_input_snapshot_t power = {0};
    board_get_v2_power_input_snapshot(&power);
    bool usb_power_present = power.usb_power_present;
    bool charger_active = power.bat_chg_level == 0;
    bool raw_full = power.bat_std_level == 0;
    bool charging = charger_active;
    bool charge_full = raw_full && !charging;
    bool charge_power_present = usb_power_present || charger_active || charge_full;
    bool external_power_present = charge_power_present;
    uint8_t neutral_legacy_brightness = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
    uint32_t active_low_power_idle_ms = external_power_present
        ? snapshot.plugged_low_power_idle_ms
        : snapshot.battery_low_power_idle_ms;
    uint32_t active_auto_shutdown_ms = external_power_present
        ? snapshot.plugged_auto_shutdown_ms
        : snapshot.battery_auto_shutdown_ms;

    printf(
        "~DEVICE:SETTINGS schema=listener.device_settings.v1 result=%s"
        " legacy_plugged_brightness=%u legacy_battery_brightness=%u active_power=%s neutral_legacy_brightness=%u"
        " led_status=%u led_key=%u led_ec11=%u led_edge=%u"
        " compact_set=1"
        " low_power_idle_ms=%" PRIu32
        " plugged_low_power_idle_ms=%" PRIu32 " battery_low_power_idle_ms=%" PRIu32
        " low_power_idle_mode=power_mode"
        " plugged_low_power_enabled=%u"
         " auto_shutdown_ms=%" PRIu32
         " plugged_auto_shutdown_ms=%" PRIu32 " battery_auto_shutdown_ms=%" PRIu32
         " auto_shutdown_enabled=%u auto_shutdown_mode=%s knob_rotation=%s"
         " ble_name=\"%s\" ble_name_pending=%u ble_name_apply=%s"
        " loaded_from_nvs=%u external_power_present=%u usb_power_present=%u usb_serial_jtag_sof_active=%u"
        " charger_active=%u charge_power_present=%u charging=%u charge_full=%u"
        " usb_det_adc_valid=%u usb_det_adc_mv=%d usb_det_mismatch=%u"
        " valid_ranges=legacy_brightness_0_100_maps_to_led_zones,led_zone_brightness_0_100,low_power_idle_ms_%u_%u,plugged_low_power_idle_ms_%u_%u,battery_low_power_idle_ms_%u_%u,plugged_low_power_enabled_0_1,auto_shutdown_ms_0_off_or_%u_%u,plugged_auto_shutdown_ms_off_only,battery_auto_shutdown_ms_0_off_or_%u_%u,ble_name_ascii_1_%u,knob_rotation_system_volume_screen_brightness_disabled\n",
        result != NULL ? result : "OK",
        snapshot.plugged_brightness_percent,
        snapshot.battery_brightness_percent,
        external_power_present ? "external" : "battery",
        neutral_legacy_brightness,
        snapshot.status_led_brightness_percent,
        snapshot.key_led_brightness_percent,
        snapshot.ec11_led_brightness_percent,
        snapshot.edge_led_brightness_percent,
        active_low_power_idle_ms,
        snapshot.plugged_low_power_idle_ms,
        snapshot.battery_low_power_idle_ms,
        snapshot.plugged_low_power_enabled ? 1u : 0u,
        active_auto_shutdown_ms,
        snapshot.plugged_auto_shutdown_ms,
        snapshot.battery_auto_shutdown_ms,
        active_auto_shutdown_ms != DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS ? 1u : 0u,
        active_auto_shutdown_ms != DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS
            ? (external_power_present ? "plugged" : "battery")
            : "disabled",
        ec11_rotation_control_action_name(ec11_rotation_control_get_action()),
        snapshot.ble_name,
        snapshot.ble_name_pending_restart ? 1u : 0u,
        snapshot.ble_name_pending_restart ? "restart_ble_or_reboot" : "active_or_next_advertising",
        snapshot.loaded_from_nvs ? 1u : 0u,
        external_power_present ? 1u : 0u,
        usb_power_present ? 1u : 0u,
        power.usb_serial_jtag_sof_active ? 1u : 0u,
        charger_active ? 1u : 0u,
        charge_power_present ? 1u : 0u,
        charging ? 1u : 0u,
        charge_full ? 1u : 0u,
        power.usb_det_adc_valid ? 1u : 0u,
        power.usb_det_adc_mv,
        power.usb_det_mismatch ? 1u : 0u,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS,
        (unsigned)DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS,
        (unsigned)DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS,
        (unsigned)DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS,
        (unsigned)DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS,
        (unsigned)DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS,
        (unsigned)DEVICE_SETTINGS_BLE_NAME_MAX_LEN);
    fflush(stdout);
}

static bool device_settings_parse_u32(const char *value, uint32_t *out_value)
{
    if (value == NULL || out_value == NULL || value[0] == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || end == NULL || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)parsed;
    return true;
}

static bool device_settings_ascii_iequals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    while (*left != '\0' && *right != '\0') {
        char l = *left;
        char r = *right;
        if (l >= 'A' && l <= 'Z') {
            l = (char)(l - 'A' + 'a');
        }
        if (r >= 'A' && r <= 'Z') {
            r = (char)(r - 'A' + 'a');
        }
        if (l != r) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static bool device_settings_auto_shutdown_disabled_value(const char *value)
{
    return device_settings_ascii_iequals(value, "0") ||
           device_settings_ascii_iequals(value, "off") ||
           device_settings_ascii_iequals(value, "disable") ||
           device_settings_ascii_iequals(value, "disabled") ||
           device_settings_ascii_iequals(value, "none");
}

static bool device_settings_parse_bool(const char *value, bool *out_value)
{
    if (value == NULL || out_value == NULL) {
        return false;
    }
    if (device_settings_ascii_iequals(value, "1") ||
        device_settings_ascii_iequals(value, "true") ||
        device_settings_ascii_iequals(value, "on") ||
        device_settings_ascii_iequals(value, "yes") ||
        device_settings_ascii_iequals(value, "enabled")) {
        *out_value = true;
        return true;
    }
    if (device_settings_ascii_iequals(value, "0") ||
        device_settings_ascii_iequals(value, "false") ||
        device_settings_ascii_iequals(value, "off") ||
        device_settings_ascii_iequals(value, "no") ||
        device_settings_ascii_iequals(value, "disabled")) {
        *out_value = false;
        return true;
    }
    return false;
}

static bool device_settings_parse_auto_shutdown_ms(const char *value, uint32_t *out_value)
{
    if (value == NULL || out_value == NULL || value[0] == '\0') {
        return false;
    }
    if (device_settings_auto_shutdown_disabled_value(value)) {
        *out_value = DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS;
        return true;
    }
    uint32_t parsed = 0;
    if (!device_settings_parse_u32(value, &parsed) ||
        parsed < DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS ||
        parsed > DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS) {
        return false;
    }
    *out_value = parsed;
    return true;
}

static bool device_settings_parse_percent_value(
    const char *value,
    uint8_t *out_value,
    const char *invalid_reason,
    const char **out_reason)
{
    uint32_t parsed = 0;
    if (!device_settings_parse_u32(value, &parsed) || parsed > 100U) {
        if (out_reason != NULL) {
            *out_reason = invalid_reason != NULL ? invalid_reason : "brightness_must_be_0_100";
        }
        return false;
    }
    *out_value = (uint8_t)parsed;
    return true;
}

static bool device_settings_apply_key_value(
    device_settings_config_t *config,
    const char *key,
    const char *value,
    char *error_key,
    size_t error_key_size,
    const char **out_reason)
{
    if (config == NULL || key == NULL || value == NULL || value[0] == '\0') {
        if (out_reason != NULL) {
            *out_reason = "missing_value";
        }
        return false;
    }
    snprintf(error_key, error_key_size, "%s", key);

    if (strcmp(key, "plugged_brightness") == 0 ||
        strcmp(key, "plugged_brightness_percent") == 0 ||
        strcmp(key, "external_brightness") == 0) {
        uint8_t legacy_brightness = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
        if (!device_settings_parse_percent_value(
                value, &legacy_brightness, "brightness_must_be_0_100", out_reason)) {
            return false;
        }
        config->status_led_brightness_percent = legacy_brightness;
        config->key_led_brightness_percent = legacy_brightness;
        config->ec11_led_brightness_percent = legacy_brightness;
        config->edge_led_brightness_percent = legacy_brightness;
        config->plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
        config->battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
        return true;
    }

    if (strcmp(key, "battery_brightness") == 0 ||
        strcmp(key, "battery_brightness_percent") == 0) {
        uint8_t legacy_brightness = DEVICE_SETTINGS_DEFAULT_LED_ZONE_BRIGHTNESS_PERCENT;
        if (!device_settings_parse_percent_value(
                value, &legacy_brightness, "brightness_must_be_0_100", out_reason)) {
            return false;
        }
        config->status_led_brightness_percent = legacy_brightness;
        config->key_led_brightness_percent = legacy_brightness;
        config->ec11_led_brightness_percent = legacy_brightness;
        config->edge_led_brightness_percent = legacy_brightness;
        config->plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
        config->battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
        return true;
    }

    if (strcmp(key, "led_status") == 0 ||
        strcmp(key, "ls") == 0 ||
        strcmp(key, "status_led") == 0 ||
        strcmp(key, "status_brightness") == 0 ||
        strcmp(key, "status_led_brightness") == 0) {
        return device_settings_parse_percent_value(
            value, &config->status_led_brightness_percent, "led_zone_brightness_must_be_0_100", out_reason);
    }

    if (strcmp(key, "led_key") == 0 ||
        strcmp(key, "lk") == 0 ||
        strcmp(key, "key_led") == 0 ||
        strcmp(key, "key_brightness") == 0 ||
        strcmp(key, "key_led_brightness") == 0) {
        return device_settings_parse_percent_value(
            value, &config->key_led_brightness_percent, "led_zone_brightness_must_be_0_100", out_reason);
    }

    if (strcmp(key, "led_ec11") == 0 ||
        strcmp(key, "l11") == 0 ||
        strcmp(key, "ec11_led") == 0 ||
        strcmp(key, "knob_led") == 0 ||
        strcmp(key, "knob_brightness") == 0 ||
        strcmp(key, "ec11_led_brightness") == 0 ||
        strcmp(key, "knob_led_brightness") == 0) {
        return device_settings_parse_percent_value(
            value, &config->ec11_led_brightness_percent, "led_zone_brightness_must_be_0_100", out_reason);
    }

    if (strcmp(key, "led_edge") == 0 ||
        strcmp(key, "le") == 0 ||
        strcmp(key, "edge_led") == 0 ||
        strcmp(key, "frame_led") == 0 ||
        strcmp(key, "edge_brightness") == 0 ||
        strcmp(key, "frame_brightness") == 0 ||
        strcmp(key, "edge_led_brightness") == 0 ||
        strcmp(key, "frame_led_brightness") == 0) {
        return device_settings_parse_percent_value(
            value, &config->edge_led_brightness_percent, "led_zone_brightness_must_be_0_100", out_reason);
    }

    if (strcmp(key, "auto_shutdown_ms") == 0 ||
        strcmp(key, "battery_auto_shutdown_ms") == 0 ||
        strcmp(key, "shutdown_ms") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_auto_shutdown_ms(value, &parsed)) {
            if (out_reason != NULL) {
                *out_reason = "auto_shutdown_ms_out_of_range";
            }
            return false;
        }
        config->battery_auto_shutdown_ms = parsed;
        return true;
    }

    if (strcmp(key, "plugged_auto_shutdown_ms") == 0 ||
        strcmp(key, "external_auto_shutdown_ms") == 0 ||
        strcmp(key, "usb_auto_shutdown_ms") == 0) {
        if (!device_settings_auto_shutdown_disabled_value(value)) {
            if (out_reason != NULL) {
                *out_reason = "plugged_auto_shutdown_disabled";
            }
            return false;
        }
        config->plugged_auto_shutdown_ms = DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS;
        return true;
    }

    if (strcmp(key, "low_power_idle_ms") == 0 ||
        strcmp(key, "low_power_ms") == 0 ||
        strcmp(key, "idle_ms") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            (parsed != DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS && parsed < DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS) ||
            parsed > DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS) {
            if (out_reason != NULL) {
                *out_reason = "low_power_idle_ms_out_of_range";
            }
            return false;
        }
        config->low_power_idle_ms = parsed;
        config->plugged_low_power_idle_ms = parsed;
        config->battery_low_power_idle_ms = parsed;
        return true;
    }

    if (strcmp(key, "plugged_low_power_idle_ms") == 0 ||
        strcmp(key, "external_low_power_idle_ms") == 0 ||
        strcmp(key, "usb_low_power_idle_ms") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            (parsed != DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS && parsed < DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS) ||
            parsed > DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS) {
            if (out_reason != NULL) {
                *out_reason = "plugged_low_power_idle_ms_out_of_range";
            }
            return false;
        }
        config->plugged_low_power_idle_ms = parsed;
        return true;
    }

    if (strcmp(key, "battery_low_power_idle_ms") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            (parsed != DEVICE_SETTINGS_LOW_POWER_IDLE_DISABLED_MS && parsed < DEVICE_SETTINGS_LOW_POWER_IDLE_MIN_MS) ||
            parsed > DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS) {
            if (out_reason != NULL) {
                *out_reason = "battery_low_power_idle_ms_out_of_range";
            }
            return false;
        }
        config->battery_low_power_idle_ms = parsed;
        config->low_power_idle_ms = parsed;
        return true;
    }

    if (strcmp(key, "plugged_low_power_enabled") == 0 ||
        strcmp(key, "ple") == 0 ||
        strcmp(key, "plugged_low_power") == 0 ||
        strcmp(key, "external_low_power") == 0 ||
        strcmp(key, "usb_low_power_enabled") == 0) {
        bool parsed = false;
        if (!device_settings_parse_bool(value, &parsed)) {
            if (out_reason != NULL) {
                *out_reason = "plugged_low_power_enabled_must_be_0_or_1";
            }
            return false;
        }
        config->plugged_low_power_enabled = parsed;
        return true;
    }

    if (strcmp(key, "low_power_idle_minutes") == 0 ||
        strcmp(key, "low_power_minutes") == 0 ||
        strcmp(key, "idle_minutes") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            parsed > (DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS / 60000U)) {
            if (out_reason != NULL) {
                *out_reason = "low_power_idle_minutes_out_of_range";
            }
            return false;
        }
        uint32_t ms = parsed * 60000U;
        config->low_power_idle_ms = ms;
        config->plugged_low_power_idle_ms = ms;
        config->battery_low_power_idle_ms = ms;
        return true;
    }

    if (strcmp(key, "plugged_low_power_idle_minutes") == 0 ||
        strcmp(key, "plm") == 0 ||
        strcmp(key, "external_low_power_idle_minutes") == 0 ||
        strcmp(key, "usb_low_power_idle_minutes") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            parsed > (DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS / 60000U)) {
            if (out_reason != NULL) {
                *out_reason = "plugged_low_power_idle_minutes_out_of_range";
            }
            return false;
        }
        uint32_t ms = parsed * 60000U;
        config->plugged_low_power_idle_ms = ms;
        return true;
    }

    if (strcmp(key, "battery_low_power_idle_minutes") == 0 ||
        strcmp(key, "blm") == 0) {
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) ||
            parsed > (DEVICE_SETTINGS_LOW_POWER_IDLE_MAX_MS / 60000U)) {
            if (out_reason != NULL) {
                *out_reason = "battery_low_power_idle_minutes_out_of_range";
            }
            return false;
        }
        uint32_t ms = parsed * 60000U;
        config->battery_low_power_idle_ms = ms;
        config->low_power_idle_ms = ms;
        return true;
    }

    if (strcmp(key, "auto_shutdown_minutes") == 0 ||
        strcmp(key, "battery_auto_shutdown_minutes") == 0 ||
        strcmp(key, "bam") == 0) {
        if (device_settings_auto_shutdown_disabled_value(value)) {
            config->battery_auto_shutdown_ms = DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS;
            return true;
        }
        uint32_t parsed = 0;
        if (!device_settings_parse_u32(value, &parsed) || parsed > (DEVICE_SETTINGS_AUTO_SHUTDOWN_MAX_MS / 60000U)) {
            if (out_reason != NULL) {
                *out_reason = "auto_shutdown_minutes_out_of_range";
            }
            return false;
        }
        uint32_t ms = parsed * 60000U;
        if (ms < DEVICE_SETTINGS_AUTO_SHUTDOWN_MIN_MS) {
            if (out_reason != NULL) {
                *out_reason = "auto_shutdown_minutes_out_of_range";
            }
            return false;
        }
        config->battery_auto_shutdown_ms = ms;
        return true;
    }

    if (strcmp(key, "plugged_auto_shutdown_minutes") == 0 ||
        strcmp(key, "pam") == 0 ||
        strcmp(key, "external_auto_shutdown_minutes") == 0 ||
        strcmp(key, "usb_auto_shutdown_minutes") == 0) {
        if (!device_settings_auto_shutdown_disabled_value(value)) {
            if (out_reason != NULL) {
                *out_reason = "plugged_auto_shutdown_disabled";
            }
            return false;
        }
        config->plugged_auto_shutdown_ms = DEVICE_SETTINGS_AUTO_SHUTDOWN_DISABLED_MS;
        return true;
    }

    if (strcmp(key, "ble_name") == 0 || strcmp(key, "name") == 0) {
        if (!device_settings_validate_ble_name(value)) {
            if (out_reason != NULL) {
                *out_reason = "ble_name_ascii_1_29_no_quotes_semicolon_equals";
            }
            return false;
        }
        snprintf(config->ble_name, sizeof(config->ble_name), "%s", value);
        return true;
    }

    if (strcmp(key, "knob_rotation") == 0 ||
        strcmp(key, "ec11_rotation") == 0 ||
        strcmp(key, "rotation_action") == 0) {
        ec11_rotation_action_t action = EC11_ROTATION_ACTION_SYSTEM_VOLUME;
        if (!ec11_rotation_control_parse_action(value, &action)) {
            if (out_reason != NULL) {
                *out_reason = "knob_rotation_must_be_system_volume_screen_brightness_disabled";
            }
            return false;
        }
        config->knob_rotation_action = (uint8_t)action;
        return true;
    }

    if (out_reason != NULL) {
        *out_reason = "unknown_key";
    }
    return false;
}

static esp_err_t device_settings_apply_set_command(const char *arguments)
{
    if (!device_settings_ensure_mutex()) {
        device_settings_print_error("system", "no_mutex");
        return ESP_ERR_NO_MEM;
    }
    if (arguments == NULL || arguments[0] == '\0') {
        device_settings_print_error("SET", "missing_arguments");
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(arguments) >= DEVICE_SETTINGS_COMMAND_BUFFER_BYTES) {
        device_settings_print_error("SET", "command_too_long");
        return ESP_ERR_INVALID_SIZE;
    }

    char buffer[DEVICE_SETTINGS_COMMAND_BUFFER_BYTES];
    snprintf(buffer, sizeof(buffer), "%s", arguments);

    esp_err_t ret = ESP_OK;
    bool ok = true;
    bool saw_token = false;
    char error_key[48] = "SET";
    const char *error_reason = "invalid";

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        (void)device_settings_ensure_loaded_locked();
        device_settings_config_t next = s_settings;

        char *save = NULL;
        for (char *token = strtok_r(buffer, " ", &save);
             token != NULL;
             token = strtok_r(NULL, " ", &save)) {
            if (token[0] == '\0') {
                continue;
            }
            saw_token = true;
            char *equals = strchr(token, '=');
            if (equals == NULL) {
                snprintf(error_key, sizeof(error_key), "%s", token);
                error_reason = "expected_key_value";
                ok = false;
                break;
            }
            *equals = '\0';
            const char *key = token;
            const char *value = equals + 1;
            if (!device_settings_apply_key_value(
                    &next,
                    key,
                    value,
                    error_key,
                    sizeof(error_key),
                    &error_reason)) {
                ok = false;
                break;
            }
        }

        if (!saw_token) {
            ok = false;
            snprintf(error_key, sizeof(error_key), "SET");
            error_reason = "missing_arguments";
        }

        if (ok) {
            bool name_changed = strcmp(s_settings.ble_name, next.ble_name) != 0;
            next.plugged_brightness_percent = DEVICE_SETTINGS_DEFAULT_PLUGGED_BRIGHTNESS_PERCENT;
            next.battery_brightness_percent = DEVICE_SETTINGS_DEFAULT_BATTERY_BRIGHTNESS_PERCENT;
            s_settings = next;
            if (name_changed) {
                s_ble_name_pending_restart = true;
            }
            ret = device_settings_persist_locked(true);
            if (ret != ESP_OK) {
                ok = false;
                snprintf(error_key, sizeof(error_key), "persist");
                error_reason = esp_err_to_name(ret);
            } else {
                device_settings_apply_runtime_locked("device_settings");
            }
        }
        xSemaphoreGive(s_mutex);
    } else {
        ok = false;
        snprintf(error_key, sizeof(error_key), "SET");
        error_reason = "lock_timeout";
        ret = ESP_ERR_TIMEOUT;
    }

    if (!ok) {
        device_settings_print_error(error_key, error_reason);
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_ARG;
    }

    device_settings_print_status("OK");
    return ESP_OK;
}

esp_err_t device_settings_consume_control_command(const char *line)
{
    const char *command = device_settings_strip_prefix(line);
    if (command == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (strcmp(command, "SETTINGS") == 0 || strcmp(command, "STATUS") == 0) {
        device_settings_print_status("OK");
        return ESP_OK;
    }

    if (strncmp(command, "SET ", strlen("SET ")) == 0) {
        return device_settings_apply_set_command(command + strlen("SET "));
    }

    if (strcmp(command, "RESET") == 0) {
        if (!device_settings_ensure_mutex()) {
            device_settings_print_error("RESET", "no_mutex");
            return ESP_ERR_NO_MEM;
        }
        esp_err_t ret = ESP_OK;
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            char old_name[DEVICE_SETTINGS_BLE_NAME_MAX_LEN + 1];
            snprintf(old_name, sizeof(old_name), "%s", s_settings.ble_name);
            device_settings_set_defaults_locked();
            if (strcmp(old_name, s_settings.ble_name) != 0) {
                s_ble_name_pending_restart = true;
            }
            ret = device_settings_persist_locked(true);
            if (ret == ESP_OK) {
                device_settings_apply_runtime_locked("device_settings_reset");
            }
            xSemaphoreGive(s_mutex);
        } else {
            ret = ESP_ERR_TIMEOUT;
        }
        if (ret != ESP_OK) {
            device_settings_print_error("RESET", esp_err_to_name(ret));
        } else {
            device_settings_print_status("RESET");
        }
        return ret;
    }

    if (strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0) {
        printf("~DEVICE:HELP commands=SETTINGS,STATUS,SET,RESET keys=led_status,led_key,led_ec11,led_edge,low_power_idle_ms,low_power_idle_minutes,plugged_low_power_idle_ms,plugged_low_power_idle_minutes,battery_low_power_idle_ms,battery_low_power_idle_minutes,plugged_low_power_enabled,auto_shutdown_ms,auto_shutdown_minutes,battery_auto_shutdown_ms,battery_auto_shutdown_minutes,ble_name,knob_rotation compact_set=1 compact_keys=ls,lk,l11,le,plm,blm,ple,bam legacy_keys=plugged_brightness,battery_brightness\n");
        fflush(stdout);
        return ESP_OK;
    }

    device_settings_print_error(command, "unknown_command");
    return ESP_ERR_INVALID_ARG;
}

bool device_settings_consume_usb_command(const char *line)
{
    return device_settings_consume_control_command(line) != ESP_ERR_NOT_FOUND;
}
