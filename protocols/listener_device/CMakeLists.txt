idf_component_register(
    SRCS listener_device.c
    INCLUDE_DIRS include
    REQUIRES app_update device_settings power_manager
)
