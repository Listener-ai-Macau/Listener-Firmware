#include "diag_log.h"
#include "diag_log_platform.h"

void diag_log_init(void)
{
    diag_log_platform_init();
}

void diag_log_write(uint16_t source, uint8_t event, uint8_t severity,
                    uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    diag_log_platform_write(source, event, severity, arg1, arg2, arg3, arg4);
}

uint32_t diag_log_count(void)
{
    return diag_log_platform_count();
}

void diag_log_dump(void)
{
    diag_log_platform_dump();
}

void diag_log_dump_last(uint32_t count)
{
    diag_log_platform_dump_last(count);
}

void diag_log_clear(void)
{
    diag_log_platform_clear();
}
