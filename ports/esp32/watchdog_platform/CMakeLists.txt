idf_component_register(
    SRCS "watchdog_platform_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_system freertos
)
