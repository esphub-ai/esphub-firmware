// SPDX-License-Identifier: MIT
#include "DEV_Config.h"
#include "driver/spi_master.h"

static spi_device_handle_t screen;

void example_epaper_begin()
{
    example_output(EPD_CS_PIN, 1);
    example_output(EPD_DC_PIN, 0);
    example_output(EPD_RST_PIN, 1);
    ESP_ERROR_CHECK(gpio_set_direction(EPD_BUSY_PIN, GPIO_MODE_INPUT));
    spi_bus_config_t pins = {};
    pins.mosi_io_num = EPD_MOSI_PIN;
    pins.miso_io_num = -1;
    pins.sclk_io_num = EPD_SCK_PIN;
    pins.quadwp_io_num = -1;
    pins.quadhd_io_num = -1;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &pins, SPI_DMA_DISABLED));
    spi_device_interface_config_t config = {};
    config.clock_speed_hz = 4000000;
    config.spics_io_num = -1;
    config.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &config, &screen));
}

void DEV_SPI_WriteByte(UBYTE byte)
{
    spi_transaction_t word = {};
    word.length = 8;
    word.flags = SPI_TRANS_USE_TXDATA;
    word.tx_data[0] = byte;
    ESP_ERROR_CHECK(spi_device_polling_transmit(screen, &word));
}

void DEV_SPI_Write_nByte(UBYTE *data, UDOUBLE length)
{
    while (length) {
        // Without DMA, ESP32-S3 can transfer at most 64 bytes per transaction.
        unsigned count = length > 64 ? 64 : length;
        spi_transaction_t part = {};
        part.length = count * 8;
        part.tx_buffer = data;
        ESP_ERROR_CHECK(spi_device_polling_transmit(screen, &part));
        length -= count;
        data += count;
    }
}
