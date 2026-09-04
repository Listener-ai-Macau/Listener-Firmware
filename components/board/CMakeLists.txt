idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins battery_monitor diag_log esp_driver_gpio esp_driver_usb_serial_jtag esp_timer watchdog_platform
    PRIV_REQUIRES hal
)
