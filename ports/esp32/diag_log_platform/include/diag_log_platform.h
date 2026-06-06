#ifndef DIAG_LOG_PLATFORM_H
#define DIAG_LOG_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifndef DIAG_LOG_EVENT_WIRE_BYTES
#define DIAG_LOG_EVENT_WIRE_BYTES 24U
#endif

void diag_log_platform_init(void);
void diag_log_platform_write(uint16_t source, uint8_t event, uint8_t severity,
                             uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
uint32_t diag_log_platform_count(void);
void diag_log_platform_dump(void);
void diag_log_platform_dump_last(uint32_t count);
void diag_log_platform_dump_last_by_source(uint32_t count, uint16_t source);
void diag_log_platform_clear(void);
bool diag_log_platform_is_dumping(void);

/*
 * Read a contiguous range of retained events into a caller-supplied buffer.
 * Returns the number of events actually written. Events are packed 24-byte
 * diag_event_t structs. Thread-safe (acquires s_mutex).
 */
uint32_t diag_log_platform_read_range(uint32_t offset, uint32_t limit,
                                       void *buffer, uint32_t buffer_size);

#endif /* DIAG_LOG_PLATFORM_H */
