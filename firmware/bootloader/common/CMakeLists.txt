cmake_minimum_required(VERSION 3.16)
# Each variant uses this file as its project entry point.
get_filename_component(ESPHUB_BL "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
if(NOT ESPHUB_BL MATCHES "^([a-z0-9]+)-([0-9]+)mb(-uart|-opi)?$")
    message(FATAL_ERROR "Invalid bootloader target, capacity, or hardware variant.")
endif()
set(IDF_TARGET "${CMAKE_MATCH_1}")
set(variant_flash "${CMAKE_MATCH_2}MB")
set(variant_bus "${CMAKE_MATCH_3}")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../common/main" "${CMAKE_CURRENT_SOURCE_DIR}/../../components")
set(SDKCONFIG_DEFAULTS "../../sdkconfig.common;../common/sdkconfig.defaults;sdkconfig.defaults")
if(NOT DEFINED PROJECT_VER)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../version.txt" _eh_base LIMIT_COUNT 1)
    set(PROJECT_VER "${_eh_base}.0-dev")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-bootloader)

if(NOT CONFIG_EH_LAUNCHER OR
   NOT "${CONFIG_EH_BL_PROFILE}" STREQUAL "${ESPHUB_BL}" OR
   NOT "${CONFIG_ESPTOOLPY_FLASHSIZE}" STREQUAL "${variant_flash}" OR
   CONFIG_SPIRAM OR
   (variant_bus STREQUAL "-uart" AND NOT CONFIG_ESP_CONSOLE_UART_DEFAULT) OR
   (variant_bus STREQUAL "-opi" AND NOT CONFIG_ESPTOOLPY_FLASHMODE_OPI))
    message(FATAL_ERROR "Use a clean sdkconfig for bootloader variant '${ESPHUB_BL}'.")
endif()

# Keep EspHub's provisioning and recovery logs when trimming ESP-IDF diagnostics.
if(CONFIG_LOG_DEFAULT_LEVEL LESS 3)
    idf_component_get_property(agent_library eh_agent COMPONENT_LIB)
    idf_component_get_property(main_library main COMPONENT_LIB)
    target_compile_definitions(${agent_library} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_INFO)
    target_compile_definitions(${main_library} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_INFO)
endif()
