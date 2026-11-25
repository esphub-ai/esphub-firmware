if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(json_component espressif__cjson)
    set(mqtt_component espressif__mqtt)
else()
    set(json_component json)
    set(mqtt_component mqtt)
endif()

if(CONFIG_OKOU_BLE_WIFI)
    set(ble_source "src/okou_ble.c")
endif()
idf_component_register(
    SRCS ${ble_source} "src/okou_agent.c" "src/okou_console.c" "src/okou_hub.c" "src/okou_iot.c" "src/okou_registry.c" "src/okou_net.c" "src/okou_wifi.c" "src/okou_wifi_setup.c" "src/okou_util.c" "src/okou_qemu.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_partition esp_http_client ${json_component} esp_event
    PRIV_REQUIRES esp_pm bt ${mqtt_component} app_update nvs_flash esp_netif esp_wifi esp_eth esp_timer esp-tls mbedtls
                  esp_app_format esp_system log heap)

if(CONFIG_OKOU_QEMU_SYSTEM_RESET)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart_noos")
endif()
