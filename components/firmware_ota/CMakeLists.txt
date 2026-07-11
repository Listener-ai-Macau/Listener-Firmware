idf_component_register(
    SRCS firmware_ota.c
    INCLUDE_DIRS include
    REQUIRES app_update spi_flash diag_log listener_device audio_capture ble_audio_stream status_led power_manager nvs_flash
)
