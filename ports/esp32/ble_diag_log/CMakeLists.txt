idf_component_register(
    SRCS
        "ble_diag_log_esp32.c"
        "../../../third_party/denzic-platform/observability/embedded/c/src/denzic_diag_log_gatt_v1.c"
    INCLUDE_DIRS
        "include"
        "../../../third_party/denzic-platform/observability/embedded/c/include"
    REQUIRES bt diag_log
    PRIV_REQUIRES power_manager
)
