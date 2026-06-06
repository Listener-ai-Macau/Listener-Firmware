idf_component_register(
    SRCS diag_log_flash.c
    INCLUDE_DIRS include
    REQUIRES esp_partition spi_flash esp_timer freertos watchdog_platform
)
