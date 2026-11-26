#pragma once

#include <M5Unified.h>
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace cardputer {

inline sdmmc_card_t* mountCard()
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus{};
    bus.mosi_io_num = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    bus.miso_io_num = M5.getPin(m5::pin_name_t::sd_spi_miso);
    bus.sclk_io_num = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    bus.quadwp_io_num = bus.quadhd_io_num = -1;
    auto spi = static_cast<spi_host_device_t>(host.slot);
    esp_err_t err = spi_bus_initialize(spi, &bus, SPI_DMA_CH_AUTO);
    sdmmc_card_t* card = nullptr;
    if (err == ESP_OK) {
        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = spi;
        slot.gpio_cs = static_cast<gpio_num_t>(M5.getPin(m5::pin_name_t::sd_spi_cs));
        esp_vfs_fat_sdmmc_mount_config_t mount{};
        mount.max_files = 2;
        err = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &card);
        if (err != ESP_OK) spi_bus_free(spi);
    }
    if (err != ESP_OK) M5.Display.printf("SD mount failed: %s\n", esp_err_to_name(err));
    return card;
}

} // namespace cardputer
