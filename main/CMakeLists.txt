idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES keyboard ble_hid self_test system_health diag_log
)
