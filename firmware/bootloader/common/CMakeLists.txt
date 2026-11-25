cmake_minimum_required(VERSION 3.16)
# Each variant uses this file as its project entry point.
get_filename_component(ESPHUB_BL "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
if(NOT ESPHUB_BL MATCHES "^([a-z0-9]+)-([0-9]+)mb(-usb-cdc)?$")
    message(FATAL_ERROR "Build from firmware/bootloader/<target>-<size>mb or <target>-<size>mb-usb-cdc.")
endif()
set(IDF_TARGET "${CMAKE_MATCH_1}")
set(variant_flash "${CMAKE_MATCH_2}MB")
set(variant_transport "${CMAKE_MATCH_3}")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../common/main" "${CMAKE_CURRENT_SOURCE_DIR}/../../components")
set(SDKCONFIG_DEFAULTS "../../sdkconfig.common;../common/sdkconfig.defaults;sdkconfig.defaults;../../ble.defaults")
if(NOT DEFINED PROJECT_VER)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../version.txt" _okl_base LIMIT_COUNT 1)
    set(PROJECT_VER "${_okl_base}.0-dev")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-bootloader)

if(NOT "${CONFIG_OKOU_BL_PROFILE}" STREQUAL "${ESPHUB_BL}" OR
   NOT "${CONFIG_ESPTOOLPY_FLASHSIZE}" STREQUAL "${variant_flash}" OR
   NOT "${CONFIG_OKOU_BOARD_PROFILE}" STREQUAL "" OR CONFIG_SPIRAM)
    message(FATAL_ERROR "Use a clean sdkconfig for bootloader variant '${ESPHUB_BL}'.")
endif()

if((variant_transport STREQUAL "-usb-cdc" AND NOT CONFIG_OKL_USB_CDC) OR
   (variant_transport STREQUAL "" AND CONFIG_OKL_USB_CDC))
    message(FATAL_ERROR "USB CDC settings do not match bootloader variant '${ESPHUB_BL}'.")
endif()

# Let the SDK omit app-only command and boot paths from the bootloader image.
idf_component_get_property(agent_library okou_agent COMPONENT_LIB)
target_compile_definitions(${agent_library} PRIVATE OKOU_BOOTLOADER_BUILD)
