idf_component_register(SRCS "${EH_ENTRYPOINT}"
    INCLUDE_DIRS "."
    REQUIRES eh_agent driver spi_flash esp_timer esp_hw_support)
