idf_component_register(
    SRCS "voice_recording_control.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_capture voice_key_input ble_hid_gap
)
