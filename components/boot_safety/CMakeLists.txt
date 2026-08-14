idf_component_register(
    SRCS "boot_safety.c"
    INCLUDE_DIRS "include"
    REQUIRES device_health_platform diag_log esp_app_format esp_system freertos
)
