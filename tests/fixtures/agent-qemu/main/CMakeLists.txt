idf_component_register(SRCS "probe.c" INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../../../../firmware/components/okou_agent/src"
    REQUIRES okou_agent nvs_flash esp_timer)
foreach(symbol xQueueSemaphoreTake okou_net_start okou_net_has_ip
        okou_iot_start okou_iot_stop okou_iot_events okou_iot_publish okou_iot_control)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
endforeach()
