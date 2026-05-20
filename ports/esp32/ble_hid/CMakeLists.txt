idf_component_register(
    SRCS "ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hid nvs_flash
    PRIV_REQUIRES bt esp_adc esp_driver_usb_serial_jtag hid_keyboard board ble_hid_gap voice_recording_control ble_audio_stream listener_device
)
