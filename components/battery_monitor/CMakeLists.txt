idf_component_register(
    SRCS "battery_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins
    PRIV_REQUIRES esp_adc freertos nvs_flash
)
