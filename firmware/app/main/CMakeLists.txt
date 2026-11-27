if(EH_APP_NAME STREQUAL "atomic-poe-mqtt")
    set(example_requires mqtt)
endif()
idf_component_register(SRC_DIRS "${EH_EXAMPLE_DIR}"
    INCLUDE_DIRS "${EH_EXAMPLE_DIR}" "include"
    REQUIRES ${example_requires} eh_agent driver esp_eth esp_netif esp_lcd spi_flash esp_timer esp_hw_support)
