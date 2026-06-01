idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES keyboard ble_hid board self_test system_health diag_log firmware_ota power_manager status_led esp_pm watchdog_platform boot_safety
)

if(DEFINED ENV{LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL} AND NOT "$ENV{LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LISTENER_OTA_FORCE_PENDING_VERIFY_FAIL=1)
endif()
