idf_component_register(
    SRCS
        "diag_log_flash.c"
        "../../../third_party/denzic-platform/observability/embedded/c/src/denzic_diag_log_store.c"
    INCLUDE_DIRS
        "include"
        "../../../third_party/denzic-platform/observability/embedded/c/include"
    REQUIRES esp_partition spi_flash esp_timer freertos watchdog_platform
)
