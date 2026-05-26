idf_component_register(
    SRCS "ble_firmware_ota_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES bt firmware_ota
    PRIV_REQUIRES diag_log
)
