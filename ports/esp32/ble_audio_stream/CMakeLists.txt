idf_component_register(
    SRCS
        "ble_audio_stream_esp32.c"
        "../../../third_party/denzic-platform/audio/embedded/c/src/denzic_audio_lossless_v1.c"
    INCLUDE_DIRS
        "include"
        "../../../third_party/denzic-platform/audio/embedded/c/include"
    REQUIRES listener_proto bt
    PRIV_REQUIRES mbedtls listener_device diag_log status_led watchdog_platform
)
