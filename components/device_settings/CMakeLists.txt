idf_component_register(
    SRCS "device_settings.c" "device_key_profile.c"
    INCLUDE_DIRS "include"
    REQUIRES board ec11_rotation_control nvs_flash freertos esp_hw_support json
)
