# CMakeLists.txt for ESP32-C6 Matter/Thread SCD40 Air Quality Sensor with OLED Display
#
# This project creates a Matter air quality sensor device that:
# - Reads CO2, temperature, and humidity from an SCD40 sensor via I2C
# - Displays readings on an SSD1306 128x64 OLED display
# - Communicates over Thread network using Matter protocol
# - Integrates with OpenHAB via Matter binding

cmake_minimum_required(VERSION 3.16)

# Set ESP-Matter path if not already set
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(WARNING "ESP_MATTER_PATH not set, assuming default location")
    set(ENV{ESP_MATTER_PATH} "/workspace/esp-matter")
endif()

# Set device type to hollow (generic device for custom projects)
if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    set(ENV{ESP_MATTER_DEVICE_PATH} "$ENV{ESP_MATTER_PATH}/device_hal/device/hollow")
endif()

# Set project version
set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

# Set ESP-Matter and Matter SDK paths
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# Set target to ESP32-C6
set(IDF_TARGET "esp32c6")

# Include necessary cmake files - order is important
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

# Add extra component directories
set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append}
)

# Project name
project(matter_scd40_oled)

# Set Matter configuration options
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
