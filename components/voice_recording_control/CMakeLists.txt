idf_component_register(
    SRCS "voice_recording_control.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_capture voice_key_input ble_audio_stream ble_hid_gap device_settings diag_log power_manager status_led voice_activation_platform watchdog_platform
)
