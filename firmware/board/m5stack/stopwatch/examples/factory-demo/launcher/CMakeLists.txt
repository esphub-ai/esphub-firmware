if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)
    FetchContent_Declare(stopwatch_demo
        URL https://codeload.github.com/m5stack/M5StopWatch-UserDemo/tar.gz/6b4aa125288b6fe9dca661f10159f6e1e5ee785c
        URL_HASH SHA256=9ef07a5bef862b36b56f3a040eca2fa38b55c17fb4d692140358cbd6cec50edd
        PATCH_COMMAND git apply --no-index --whitespace=nowarn "${CMAKE_CURRENT_LIST_DIR}/esphub.patch")
    FetchContent_GetProperties(stopwatch_demo)
    if(NOT stopwatch_demo_POPULATED)
        FetchContent_Populate(stopwatch_demo)
    endif()

    # These two drivers use the I2C bus compatibility patches shipped in V0.5.
    FetchContent_Declare(stopwatch_pm1
        URL https://codeload.github.com/m5stack/M5PM1/tar.gz/8f1f1a60b3040088cd0ca2b9eb9d024ccc4d907f
        URL_HASH SHA256=78cfdbe8e1c71c4b03b54e10c7c681fa3bcd4e2d4dc6dfd58992c6015d1c1637
        PATCH_COMMAND git apply --no-index --whitespace=nowarn "${stopwatch_demo_SOURCE_DIR}/patches/M5PM1.patch")
    FetchContent_Declare(stopwatch_ioe1
        URL https://codeload.github.com/m5stack/M5IOE1/tar.gz/37db04861687858b6748f5dbd1a84439a6635c46
        URL_HASH SHA256=696d6a2e0eeddfda74481018a67ab2eb6619d1bf9e0a810a19dc040aad812750
        PATCH_COMMAND git apply --no-index --whitespace=nowarn "${stopwatch_demo_SOURCE_DIR}/patches/M5IOE1.patch")
    foreach(driver stopwatch_pm1 stopwatch_ioe1)
        FetchContent_GetProperties(${driver})
        if(NOT ${driver}_POPULATED)
            FetchContent_Populate(${driver})
        endif()
    endforeach()

    set(demo "${stopwatch_demo_SOURCE_DIR}/main")
    file(GLOB_RECURSE sources "${demo}/apps/*.cpp" "${demo}/apps/*.c"
                              "${demo}/assets/*.c" "${demo}/hal/*.c"
                              "${demo}/hal/*.cc" "${demo}/hal/*.cpp")
    list(REMOVE_ITEM sources "${demo}/hal/hal_fs.cpp" "${demo}/hal/utils/wear_levelling/wear_levelling.c")
    list(APPEND sources "${demo}/main.cpp" "storage.cpp"
        "${stopwatch_pm1_SOURCE_DIR}/src/M5PM1.cpp" "${stopwatch_ioe1_SOURCE_DIR}/src/M5IOE1.cpp")
    set_source_files_properties("${demo}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "app_main=stopwatch_factory_main")
    set(includes "${demo}" "${stopwatch_pm1_SOURCE_DIR}/src" "${stopwatch_ioe1_SOURCE_DIR}/src")
    set(sound "${demo}/assets/sfx/boot_sfx.bin")
    set(web_page "${demo}/hal/utils/config_ap/assets/badge_config_ap.html")
endif()

idf_component_register(SRCS ${sources} PRIV_INCLUDE_DIRS ${includes}
    PRIV_REQUIRES driver esp_wifi esp_netif esp_http_server esp_timer nvs_flash esp_psram esp_partition
    EMBED_FILES ${sound} EMBED_TXTFILES ${web_page})
