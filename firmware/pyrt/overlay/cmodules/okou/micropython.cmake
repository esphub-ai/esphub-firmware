# SPDX-License-Identifier: MIT
# `okou` MicroPython module + okl_runtime_start() (the okou_agent SDK is an IDF component that
# the main component REQUIRES; see apply_overlay.py).
add_library(usermod_okou INTERFACE)
target_sources(usermod_okou INTERFACE ${CMAKE_CURRENT_LIST_DIR}/modokou.c)
target_include_directories(usermod_okou INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(usermod INTERFACE usermod_okou)
