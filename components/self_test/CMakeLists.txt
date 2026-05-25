idf_component_register(
    SRCS self_test.c self_test_platform_esp32.c
    INCLUDE_DIRS include
    REQUIRES listener_device diag_log
    PRIV_REQUIRES nvs_flash
)
