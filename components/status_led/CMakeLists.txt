idf_component_register(
    SRCS "status_led.c" "status_led_strip_backend.c"
    INCLUDE_DIRS "include"
    REQUIRES board board_pins diag_log battery_monitor power_manager device_settings nvs_flash
    PRIV_REQUIRES esp_driver_gpio esp_driver_rmt esp_driver_spi esp_timer
)
