set(listener_vadnet_model_dir
    "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/espressif__esp-sr/model/vadnet_model/vadnet1_medium")
set(listener_vadnet_model_bin
    "${CMAKE_CURRENT_BINARY_DIR}/listener_vadnet1_medium.bin")
execute_process(
    COMMAND
        "${PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/pack_embedded_vadnet.py"
        --model-dir "${listener_vadnet_model_dir}"
        --output "${listener_vadnet_model_bin}"
    RESULT_VARIABLE listener_vadnet_pack_result
)
if(NOT listener_vadnet_pack_result EQUAL 0)
    message(FATAL_ERROR "Failed to pack embedded Listener VADNet model")
endif()

idf_component_register(
    SRCS "audio_capture_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_codec_dev ble_audio_stream board_pins espressif__esp-sr
    PRIV_REQUIRES esp_driver_i2c esp_driver_i2s esp_driver_usb_serial_jtag mbedtls diag_log status_led watchdog_platform
    EMBED_FILES "${listener_vadnet_model_bin}"
)
