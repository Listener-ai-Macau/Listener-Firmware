idf_component_register(
    SRCS system_health_esp32.c
    INCLUDE_DIRS include
    REQUIRES diag_log ble_hid audio_capture keyboard voice_recording_control watchdog_platform
)
