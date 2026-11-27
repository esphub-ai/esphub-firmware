if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(mqtt_component espressif__mqtt)
else()
    set(mqtt_component mqtt)
endif()
idf_component_register(SRC_DIRS "${EH_EXAMPLE_DIR}"
    INCLUDE_DIRS "${EH_EXAMPLE_DIR}" "include"
    REQUIRES eh_agent driver esp_eth esp_netif esp_lcd spi_flash esp_timer esp_hw_support ${mqtt_component})
