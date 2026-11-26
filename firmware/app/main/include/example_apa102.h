// SPDX-License-Identifier: MIT
#pragma once

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void example_apa102_blink(int data_pin, int clock_pin, unsigned count)
{
    spi_bus_config_t pins = {
        .mosi_io_num = data_pin, .miso_io_num = -1, .sclk_io_num = clock_pin,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    spi_device_interface_config_t config = {
        .clock_speed_hz = 1000000, .spics_io_num = -1, .queue_size = 1,
    };
    spi_device_handle_t device;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &pins, SPI_DMA_DISABLED));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &config, &device));
    bool on = true;
    for (;;) {
        spi_transaction_t word = {.flags = SPI_TRANS_USE_TXDATA, .length = 32};
        ESP_ERROR_CHECK(spi_device_polling_transmit(device, &word)); // Start frame
        word.tx_data[0] = 0xe3; // Three of 31 brightness steps
        word.tx_data[1] = on ? 128 : 0; // Blue, green, red
        for (unsigned i = 0; i < count; ++i) {
            ESP_ERROR_CHECK(spi_device_polling_transmit(device, &word));
        }
        for (int i = 0; i < 4; ++i) word.tx_data[i] = 0xff;
        ESP_ERROR_CHECK(spi_device_polling_transmit(device, &word)); // End frame, up to 64 LEDs
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
