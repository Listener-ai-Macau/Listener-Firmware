#include "diag_log.h"
#include "diag_log_platform.h"

#include "freertos/FreeRTOS.h"

static bool s_dumping;
static portMUX_TYPE s_dumping_lock = portMUX_INITIALIZER_UNLOCKED;

static void diag_log_set_dumping(bool dumping)
{
    portENTER_CRITICAL(&s_dumping_lock);
    s_dumping = dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
}

static bool diag_log_get_dumping(void)
{
    portENTER_CRITICAL(&s_dumping_lock);
    bool dumping = s_dumping;
    portEXIT_CRITICAL(&s_dumping_lock);
    return dumping;
}

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
    diag_log_set_dumping(true);
    diag_log_platform_dump();
    diag_log_set_dumping(false);
}

void diag_log_dump_last(uint32_t count)
{
    diag_log_set_dumping(true);
    diag_log_platform_dump_last(count);
    diag_log_set_dumping(false);
}

void diag_log_clear(void)
{
    diag_log_platform_clear();
}

bool diag_log_is_dumping(void)
{
    return diag_log_get_dumping() || diag_log_platform_is_dumping();
}

uint32_t diag_log_read_range(uint32_t offset, uint32_t limit,
                              void *buffer, uint32_t buffer_size)
{
    return diag_log_platform_read_range(offset, limit, buffer, buffer_size);
}
