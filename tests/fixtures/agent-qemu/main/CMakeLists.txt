idf_component_register(SRCS "probe.c" INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../../../../firmware/components/eh_agent/src"
    REQUIRES eh_agent nvs_flash esp_timer)
foreach(symbol xQueueSemaphoreTake eh_net_start eh_net_has_ip
        eh_iot_start eh_iot_stop eh_iot_events eh_iot_publish eh_iot_control)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
endforeach()
