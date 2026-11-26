if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(json_component espressif__cjson)
    set(mqtt_component espressif__mqtt)
else()
    set(json_component json)
    set(mqtt_component mqtt)
endif()

if(CONFIG_EH_BLE_WIFI)
    set(ble_source "src/eh_ble.c")
endif()
idf_component_register(
    SRCS ${ble_source} "src/eh_agent.c" "src/eh_console.c" "src/eh_hub.c" "src/eh_iot.c" "src/eh_registry.c" "src/eh_net.c" "src/eh_wifi.c" "src/eh_wifi_setup.c" "src/eh_util.c" "src/eh_qemu.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_partition esp_http_client ${json_component} esp_event
    PRIV_REQUIRES esp_pm bt ${mqtt_component} app_update nvs_flash esp_netif esp_wifi esp_eth esp_timer esp-tls mbedtls
                  esp_app_format esp_system log heap)

if(CONFIG_EH_QEMU_SYSTEM_RESET)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart_noos")
endif()
