#ifndef BOOT_SAFETY_H
#define BOOT_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_reset_reason_t reset_reason;
    uint32_t crash_count;
    bool safe_mode;
    bool clear_scheduled;
} boot_safety_status_t;

void boot_safety_init(void);
bool boot_safety_is_safe_mode(void);
void boot_safety_get_status(boot_safety_status_t *status);
void boot_safety_start_normal_boot_clear_timer(void);
bool boot_safety_consume_usb_command(const char *line);
const char *boot_safety_reset_reason_name(esp_reset_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_SAFETY_H */
