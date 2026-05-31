idf_component_register(
    SRCS "keyboard.c"
    INCLUDE_DIRS "include"
    REQUIRES voice_recording_control ble_hid hid_keyboard board power_manager watchdog_platform diag_log
    PRIV_REQUIRES esp_driver_gpio
)
