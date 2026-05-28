#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t watchdog_platform_subscribe_current_task(const char *task_name);
void watchdog_platform_feed_current_task(void);
void watchdog_platform_delay_ms(uint32_t delay_ms);
uint32_t watchdog_platform_task_notify_take(BaseType_t clear_on_exit, uint32_t wait_ms);
void watchdog_platform_log_config(void);
bool watchdog_platform_consume_usb_command(const char *line);

#ifdef __cplusplus
}
#endif
