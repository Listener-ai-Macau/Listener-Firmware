#include "diag_log.h"
#include "diag_log_platform.h"

static bool s_dumping;

void diag_log_init(void)
{
    diag_log_platform_init();
    diag_log_write(DIAG_SRC_SYSTEM, DIAG_SYS_INIT_RESULT, DIAG_SEV_INFO,
                   DIAG_COMP_DIAG_LOG, 0, 0, 0);
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
    s_dumping = true;
    diag_log_platform_dump();
    s_dumping = false;
}

void diag_log_dump_last(uint32_t count)
{
    s_dumping = true;
    diag_log_platform_dump_last(count);
    s_dumping = false;
}

void diag_log_clear(void)
{
    diag_log_platform_clear();
}

bool diag_log_is_dumping(void)
{
    return s_dumping || diag_log_platform_is_dumping();
}

uint32_t diag_log_read_range(uint32_t offset, uint32_t limit,
                              void *buffer, uint32_t buffer_size)
{
    return diag_log_platform_read_range(offset, limit, buffer, buffer_size);
}
