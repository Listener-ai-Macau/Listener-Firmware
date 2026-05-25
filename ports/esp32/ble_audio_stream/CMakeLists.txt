idf_component_register(
    SRCS "ble_audio_stream_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES listener_proto bt
    PRIV_REQUIRES mbedtls listener_device diag_log
)
