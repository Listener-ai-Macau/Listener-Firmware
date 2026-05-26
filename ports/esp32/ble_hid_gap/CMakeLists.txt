idf_component_register(
    SRCS "ble_hid_gap_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hid
    PRIV_REQUIRES bt ble_audio_stream ble_firmware_ota diag_log
)
