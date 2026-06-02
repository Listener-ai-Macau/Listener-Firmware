#include "ec11_rotation_control.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "ec11_rotation";

static ec11_rotation_action_t s_action = EC11_ROTATION_ACTION_SYSTEM_VOLUME;
static ec11_rotation_dispatcher_t s_dispatcher;
static portMUX_TYPE s_action_lock = portMUX_INITIALIZER_UNLOCKED;

void ec11_rotation_control_register_dispatcher(ec11_rotation_dispatcher_t dispatcher)
{
    s_dispatcher = dispatcher;
}

ec11_rotation_action_t ec11_rotation_control_get_action(void)
{
    portENTER_CRITICAL(&s_action_lock);
    ec11_rotation_action_t action = s_action;
    portEXIT_CRITICAL(&s_action_lock);
    return action;
}

const char *ec11_rotation_control_action_name(ec11_rotation_action_t action)
{
    switch (action) {
    case EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS:
        return "screen_brightness";
    case EC11_ROTATION_ACTION_DISABLED:
        return "disabled";
    case EC11_ROTATION_ACTION_SYSTEM_VOLUME:
    default:
        return "system_volume";
    }
}

const char *ec11_rotation_control_direction_name(ec11_rotation_direction_t direction)
{
    switch (direction) {
    case EC11_ROTATION_DIRECTION_CCW:
        return "ccw";
    case EC11_ROTATION_DIRECTION_CW:
    default:
        return "cw";
    }
}

esp_err_t ec11_rotation_control_set_action(ec11_rotation_action_t action, const char *source)
{
    if (action != EC11_ROTATION_ACTION_SYSTEM_VOLUME &&
        action != EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS &&
        action != EC11_ROTATION_ACTION_DISABLED) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_action_lock);
    s_action = action;
    portEXIT_CRITICAL(&s_action_lock);

    ESP_LOGI(
        TAG,
        "EC11 rotation action set: action=%s source=%s",
        ec11_rotation_control_action_name(action),
        source != NULL ? source : "unknown");
    return ESP_OK;
}

static bool ec11_rotation_parse_action(const char *raw, ec11_rotation_action_t *out_action)
{
    if (raw == NULL || out_action == NULL) {
        return false;
    }
    if (strcmp(raw, "VOLUME") == 0 || strcmp(raw, "SYSTEM_VOLUME") == 0 ||
        strcmp(raw, "SYSTEMVOLUME") == 0) {
        *out_action = EC11_ROTATION_ACTION_SYSTEM_VOLUME;
        return true;
    }
    if (strcmp(raw, "BRIGHTNESS") == 0 || strcmp(raw, "SCREEN_BRIGHTNESS") == 0 ||
        strcmp(raw, "SCREENBRIGHTNESS") == 0) {
        *out_action = EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS;
        return true;
    }
    if (strcmp(raw, "DISABLED") == 0 || strcmp(raw, "DISABLE") == 0 ||
        strcmp(raw, "OFF") == 0 || strcmp(raw, "NONE") == 0) {
        *out_action = EC11_ROTATION_ACTION_DISABLED;
        return true;
    }
    return false;
}

static bool ec11_rotation_parse_direction(const char *raw, ec11_rotation_direction_t *out_direction)
{
    if (raw == NULL || out_direction == NULL) {
        return false;
    }
    if (strcmp(raw, "CW") == 0 || strcmp(raw, "UP") == 0 ||
        strcmp(raw, "INCREASE") == 0 || strcmp(raw, "RIGHT") == 0) {
        *out_direction = EC11_ROTATION_DIRECTION_CW;
        return true;
    }
    if (strcmp(raw, "CCW") == 0 || strcmp(raw, "DOWN") == 0 ||
        strcmp(raw, "DECREASE") == 0 || strcmp(raw, "LEFT") == 0) {
        *out_direction = EC11_ROTATION_DIRECTION_CCW;
        return true;
    }
    return false;
}

bool ec11_rotation_control_consume_command(const char *line, const char *source, esp_err_t *out_result)
{
    if (out_result != NULL) {
        *out_result = ESP_OK;
    }
    if (line == NULL) {
        return false;
    }

    const char *command = line;
    if (command[0] == '~') {
        command++;
    }
    if (strncmp(command, "EC11:", strlen("EC11:")) != 0) {
        return false;
    }
    command += strlen("EC11:");

    esp_err_t result = ESP_OK;
    if (strcmp(command, "STATUS") == 0) {
        ec11_rotation_action_t action = ec11_rotation_control_get_action();
        ESP_LOGI(TAG, "EC11 rotation status: action=%s", ec11_rotation_control_action_name(action));
    } else if (strncmp(command, "MODE:", strlen("MODE:")) == 0 ||
               strncmp(command, "ACTION:", strlen("ACTION:")) == 0) {
        const char *raw_action = strchr(command, ':');
        ec11_rotation_action_t action = EC11_ROTATION_ACTION_SYSTEM_VOLUME;
        if (raw_action == NULL || !ec11_rotation_parse_action(raw_action + 1, &action)) {
            ESP_LOGW(TAG, "EC11 rotation unknown action command=%s", command);
            result = ESP_ERR_INVALID_ARG;
        } else {
            result = ec11_rotation_control_set_action(action, source);
        }
    } else if (strncmp(command, "ROTATE:", strlen("ROTATE:")) == 0) {
        ec11_rotation_direction_t direction = EC11_ROTATION_DIRECTION_CW;
        const char *raw_direction = command + strlen("ROTATE:");
        if (!ec11_rotation_parse_direction(raw_direction, &direction)) {
            ESP_LOGW(TAG, "EC11 rotation unknown direction command=%s", command);
            result = ESP_ERR_INVALID_ARG;
        } else if (s_dispatcher == NULL) {
            ESP_LOGW(TAG, "EC11 rotation dispatcher unavailable");
            result = ESP_ERR_INVALID_STATE;
        } else {
            result = s_dispatcher(direction, source != NULL ? source : "ec11_command");
        }
    } else {
        ESP_LOGW(TAG, "EC11 rotation unknown command=%s", command);
        result = ESP_ERR_INVALID_ARG;
    }

    if (out_result != NULL) {
        *out_result = result;
    }
    return true;
}
