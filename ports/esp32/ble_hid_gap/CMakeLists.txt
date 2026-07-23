idf_component_register(
    SRCS "ble_hid_gap_esp32.c"
        "../../../third_party/denzic-platform/device_control/embedded/c/src/denzic_device_control_v1.c"
        "../../../third_party/denzic-platform/ble_pairing/embedded/c/src/denzic_ble_pairing_v1.c"
        "../../../third_party/denzic-platform/ble_pairing/embedded/c/src/denzic_ble_pairing_v1_orchestration.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../../third_party/denzic-platform/device_control/embedded/c/include"
        "../../../third_party/denzic-platform/ble_pairing/embedded/c/include"
    REQUIRES esp_hid
    PRIV_REQUIRES bt ble_audio_stream ble_firmware_ota ble_diag_log device_settings diag_log esp_timer listener_device nvs_flash status_led
)
