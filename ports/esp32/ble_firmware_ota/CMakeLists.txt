idf_component_register(
    SRCS
        "ble_firmware_ota_esp32.c"
        "../../../third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1.c"
        "../../../third_party/denzic-platform/ota/embedded/c/src/denzic_ota_v1_reorder.c"
    INCLUDE_DIRS
        "include"
        "../../../third_party/denzic-platform/ota/embedded/c/include"
    REQUIRES bt firmware_ota
    PRIV_REQUIRES audio_capture diag_log esp_timer watchdog_platform
)
