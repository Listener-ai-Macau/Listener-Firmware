idf_component_register(
    SRCS "voice_key_input_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins esp_io_expander_tca95xx_16bit
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_hw_support audio_capture ble_hid diag_log hid_keyboard power_manager status_led watchdog_platform
)
