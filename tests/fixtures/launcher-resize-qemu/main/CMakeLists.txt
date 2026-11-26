set(launcher "../../../../firmware/bootloader/common/main")
idf_component_register(SRCS "probe.c" "${launcher}/resize.c"
    INCLUDE_DIRS "." "${launcher}"
    REQUIRES eh_agent joltwallet__littlefs app_update bootloader_support spi_flash mbedtls nvs_flash)
