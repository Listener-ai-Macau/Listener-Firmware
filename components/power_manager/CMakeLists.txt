idf_component_register(
    SRCS "power_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES battery_monitor board device_settings diag_log board_pins watchdog_platform
    PRIV_REQUIRES esp_driver_gpio esp_hw_support esp_pm esp_timer
)
