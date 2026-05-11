idf_component_register(
    SRCS "keyboard.c"
    INCLUDE_DIRS "include"
    REQUIRES ble_hid hid_keyboard board
)
