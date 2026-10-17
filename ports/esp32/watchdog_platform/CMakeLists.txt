idf_component_register(
    SRCS "watchdog_platform_esp32.c" "voice_stack_diag_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_system esp_hw_support esp_app_format freertos
)
