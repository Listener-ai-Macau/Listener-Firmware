idf_component_register(
    SRCS "ble_diag_log_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES bt diag_log
    PRIV_REQUIRES power_manager
)
