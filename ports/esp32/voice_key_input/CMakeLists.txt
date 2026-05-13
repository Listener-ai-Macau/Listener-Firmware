idf_component_register(
    SRCS "voice_key_input_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_io_expander_tca95xx_16bit
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c audio_capture
)
