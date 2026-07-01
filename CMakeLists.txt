# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{LISTENER_FIRMWARE_VERSION_OVERRIDE} AND NOT "$ENV{LISTENER_FIRMWARE_VERSION_OVERRIDE}" STREQUAL "")
    set(PROJECT_VER "$ENV{LISTENER_FIRMWARE_VERSION_OVERRIDE}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VER)
    string(STRIP "${PROJECT_VER}" PROJECT_VER)
else()
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
        set(PROJECT_VER "1.0.2")
    endif()
endif()

set(EXTRA_COMPONENT_DIRS
    ports/esp32
    protocols
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(voice-keyboard-firmware)
