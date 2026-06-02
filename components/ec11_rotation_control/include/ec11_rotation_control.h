#ifndef EC11_ROTATION_CONTROL_H
#define EC11_ROTATION_CONTROL_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EC11_ROTATION_ACTION_SYSTEM_VOLUME = 0,
    EC11_ROTATION_ACTION_SCREEN_BRIGHTNESS = 1,
    EC11_ROTATION_ACTION_DISABLED = 2,
} ec11_rotation_action_t;

typedef enum {
    EC11_ROTATION_DIRECTION_CW = 1,
    EC11_ROTATION_DIRECTION_CCW = 2,
} ec11_rotation_direction_t;

typedef esp_err_t (*ec11_rotation_dispatcher_t)(
    ec11_rotation_direction_t direction,
    const char *source);

void ec11_rotation_control_register_dispatcher(ec11_rotation_dispatcher_t dispatcher);
ec11_rotation_action_t ec11_rotation_control_get_action(void);
esp_err_t ec11_rotation_control_set_action(ec11_rotation_action_t action, const char *source);
const char *ec11_rotation_control_action_name(ec11_rotation_action_t action);
const char *ec11_rotation_control_direction_name(ec11_rotation_direction_t direction);
bool ec11_rotation_control_consume_command(const char *line, const char *source, esp_err_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
