idf_component_register(
    SRCS "ble_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_hid nvs_flash
    PRIV_REQUIRES bt hid_keyboard board ble_hid_gap
)
