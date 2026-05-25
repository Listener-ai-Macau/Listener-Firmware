#ifndef DIAG_LOG_PLATFORM_H
#define DIAG_LOG_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

void diag_log_platform_init(void);
void diag_log_platform_write(uint16_t source, uint8_t event, uint8_t severity,
                             uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
uint32_t diag_log_platform_count(void);
void diag_log_platform_dump(void);
void diag_log_platform_dump_last(uint32_t count);
void diag_log_platform_clear(void);

/* Returns true if the byte was consumed as part of a ~DIAGLOG: command.
   Non-DIAGLOG ~ commands (e.g. ~VREC:*) are NOT consumed. */
bool diag_log_consume_usb_command(uint8_t input_char);

#endif /* DIAG_LOG_PLATFORM_H */
