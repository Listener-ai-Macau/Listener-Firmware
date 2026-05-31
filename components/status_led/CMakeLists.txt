idf_component_register(
    SRCS "status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins diag_log battery_monitor power_manager nvs_flash
    PRIV_REQUIRES esp_driver_gpio esp_driver_rmt esp_timer
)
