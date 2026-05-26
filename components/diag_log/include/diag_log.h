#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include "diag_log_events.h"

void diag_log_init(void);
void diag_log_write(uint16_t source, uint8_t event, uint8_t severity,
                    uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

uint32_t diag_log_count(void);
void diag_log_dump(void);
void diag_log_dump_last(uint32_t count);
void diag_log_clear(void);
bool diag_log_is_dumping(void);

#define diag_log(src, evt, sev, a1, a2, a3, a4) \
    diag_log_write((src), (evt), (sev), \
                   (uint32_t)(a1), (uint32_t)(a2), \
                   (uint32_t)(a3), (uint32_t)(a4))

#endif /* DIAG_LOG_H */
