if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)
    FetchContent_Declare(paper_frame
        URL https://codeload.github.com/m5stack/M5PaperColor-UserDemo/tar.gz/884a7b7ebe1d5f1ca55c0dc4864f7708636677ea
        URL_HASH SHA256=e75aa58a3028672a28db1b86c64935206e8a8e054ae158927514abc047166996
        PATCH_COMMAND git apply --no-index --whitespace=nowarn "${CMAKE_CURRENT_LIST_DIR}/esphub.patch")
    FetchContent_GetProperties(paper_frame)
    if(NOT paper_frame_POPULATED)
        FetchContent_Populate(paper_frame)
    endif()
    set(demo "${paper_frame_SOURCE_DIR}/main")
    file(GLOB_RECURSE sources "${demo}/apps/*.cpp" "${demo}/hal/*.cpp" "${demo}/hal/*.c")
    list(APPEND sources "${demo}/main.cpp")
    set_source_files_properties("${demo}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "app_main=paper_color_factory_main")
    set(include_dirs "${demo}" "${demo}/hal/utils/audio" "${demo}/hal/utils/image" "${demo}/hal/utils/dns_server")
endif()

idf_component_register(SRCS ${sources} PRIV_INCLUDE_DIRS ${include_dirs}
    PRIV_REQUIRES eh_agent driver esp_wifi esp_netif esp_http_server esp_http_client esp-tls
    json esp_event esp_timer mbedtls mqtt fatfs wear_levelling nvs_flash sdmmc joltwallet__littlefs)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # M5PM1 1.0.6 names its optional component M5Unified. IDF's registry
    # installs the same library under the namespaced name used by EspHub.
    idf_component_get_property(pm1 m5stack__m5pm1 COMPONENT_LIB)
    idf_component_get_property(unified m5stack__m5unified COMPONENT_LIB)
    target_link_libraries(${pm1} PRIVATE ${unified})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_ASSETS_USE_EMBEDDED=1)
    target_add_binary_data(${COMPONENT_LIB} "${demo}/apps/app_server/index.html" TEXT)
    target_add_binary_data(${COMPONENT_LIB} "${demo}/assets/PaperColor.png" BINARY)
    target_add_binary_data(${COMPONENT_LIB} "${demo}/assets/arrowClickToPowerOn.png" BINARY)
endif()
