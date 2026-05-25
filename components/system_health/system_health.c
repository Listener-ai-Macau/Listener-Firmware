#include "system_health.h"
#include "system_health_platform.h"

esp_err_t system_health_init(void)
{
    return system_health_platform_init();
}

esp_err_t system_health_start(void)
{
    return system_health_platform_start();
}
