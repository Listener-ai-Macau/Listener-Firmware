idf_component_register(
    SRCS "boot_safety.c"
    INCLUDE_DIRS "include"
    REQUIRES diag_log esp_system freertos
)
