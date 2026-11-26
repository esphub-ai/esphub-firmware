# Build the manufacturer application from the same immutable source as the board catalog.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)
    FetchContent_Declare(cardputer_demo
        URL https://codeload.github.com/m5stack/M5Cardputer-UserDemo/tar.gz/9d7190317d49ff87fa98db345b049ed77c5a1780
        URL_HASH SHA256=7b90b137ba816b52bc12573129d455f532987ea3a5abd09d24f562dbb74319a4
        PATCH_COMMAND git apply --no-index --whitespace=nowarn "${CMAKE_CURRENT_LIST_DIR}/esphub.patch")
    FetchContent_GetProperties(cardputer_demo)
    if(NOT cardputer_demo_POPULATED)
        FetchContent_Populate(cardputer_demo)
    endif()
    set(demo "${cardputer_demo_SOURCE_DIR}/main")
    file(GLOB_RECURSE sources "${demo}/hal/*.c" "${demo}/hal/*.cc" "${demo}/hal/*.cpp"
                              "${demo}/apps/*.c" "${demo}/apps/*.cc" "${demo}/apps/*.cpp")
    list(APPEND sources "${demo}/main.cpp")
    set_source_files_properties("${demo}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "app_main=factory_demo_main")
    set(includes "${demo}" "${demo}/assets" "${demo}/hal" "${demo}/apps"
        "${demo}/apps/app_repl/PikaScript/pikascript-api" "${demo}/apps/app_repl/PikaScript/pikascript-core")
endif()

idf_component_register(SRCS ${sources} PRIV_INCLUDE_DIRS ${includes}
    PRIV_REQUIRES eh_agent driver esp_wifi esp_netif esp_hid fatfs sdmmc esp_timer nvs_flash)
