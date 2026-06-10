idf_component_register(
    SRCS "device_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES board ec11_rotation_control nvs_flash
)
