cmake_minimum_required(VERSION 3.16)

# DEVICE_BUILD_ROLE comes from CMakePresets.json and is intentionally used
# only at the project level. ESP-IDF evaluates component CMakeLists.txt files
# in a separate early dependency-discovery pass where arbitrary cache
# variables are not reliably propagated.
if(DEVICE_BUILD_ROLE STREQUAL "sensor")
    set(EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/roles/sensor_role"
    )
    set(COMPONENTS
        main
        current_common
        sensor_role
    )
elseif(DEVICE_BUILD_ROLE STREQUAL "display")
    set(EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/roles/display_role"
    )
    set(COMPONENTS
        main
        current_common
        display_role
    )
else()
    message(FATAL_ERROR
        "DEVICE_BUILD_ROLE must be 'sensor' or 'display'. Use --preset sensor/display."
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(current_monitor)
