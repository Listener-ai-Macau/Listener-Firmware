idf_component_register(
    SRCS "ble_hid_gap_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hid
    PRIV_REQUIRES bt ble_audio_stream ble_firmware_ota ble_diag_log diag_log esp_timer listener_device nvs_flash status_led
)
