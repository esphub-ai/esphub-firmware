cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET esp32s31)
set(MOSAICO_UTILS_ROOT "$ENV{MOSAICO_UTILS}")
set(MOSAICO_HELLO "${MOSAICO_UTILS_ROOT}/mosaico-tools/templates/hello_world")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../components/okou_agent"
    "${MOSAICO_HELLO}/components/app_display")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mosaico_hello)
