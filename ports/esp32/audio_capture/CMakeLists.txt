idf_component_register(
    SRCS "audio_capture_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_codec_dev ble_audio_stream
    PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_usb_serial_jtag mbedtls
)
