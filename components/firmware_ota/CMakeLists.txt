idf_component_register(
    SRCS
        firmware_ota.c
        ../../third_party/denzic-platform/ota/embedded/c/src/denzic_ota_orchestration_v1.c
    INCLUDE_DIRS
        include
        ../../third_party/denzic-platform/ota/embedded/c/include
    REQUIRES app_update spi_flash diag_log listener_device audio_capture ble_audio_stream status_led power_manager nvs_flash
)
