idf_component_register(
    SRCS "ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hid nvs_flash
    PRIV_REQUIRES bt esp_driver_usb_serial_jtag hid_keyboard battery_monitor board ble_hid_gap voice_recording_control audio_capture ble_audio_stream ble_firmware_ota ble_diag_log listener_device diag_log_platform diag_log firmware_ota power_manager watchdog_platform boot_safety
)
