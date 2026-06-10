idf_component_register(
    SRCS "keyboard.c"
    INCLUDE_DIRS "include"
    REQUIRES voice_recording_control ble_hid hid_keyboard board power_manager status_led watchdog_platform diag_log device_settings ec11_rotation_control ble_audio_stream
    PRIV_REQUIRES esp_driver_gpio
)
