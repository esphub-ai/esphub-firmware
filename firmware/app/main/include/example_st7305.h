// SPDX-License-Identifier: MIT
#pragma once

#include "esp_rlcd_st7305.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"

static inline void example_st7305(int clock, int mosi, int cs, int dc, int reset)
{
    const unsigned width = 400, height = 300;
    spi_bus_config_t pins = ST7305_PANEL_BUS_SPI_CONFIG(clock, mosi, width * height / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &pins, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = dc, .cs_gpio_num = cs, .pclk_hz = 10000000, .lcd_cmd_bits = 8,
        .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &io));
    st7305_vendor_config_t geometry = {
        .hor_res = width, .ver_res = height, .col_start = 0x12, .col_end = 0x2a, .row_end = 0xc7,
    };
    esp_lcd_panel_dev_config_t config = {.reset_gpio_num = reset, .bits_per_pixel = 1, .vendor_config = &geometry};
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7305(io, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    st7305_frame_buffer_t frame;
    ESP_ERROR_CHECK(esp_rlcd_st7305_frame_buffer_init(&frame, width, height,
                                                     ST7305_PIXEL_MAPPING_LANDSCAPE, MALLOC_CAP_8BIT));
    esp_rlcd_st7305_frame_buffer_clear(&frame, ST7305_COLOR_WHITE);
    ESP_ERROR_CHECK(esp_rlcd_st7305_frame_buffer_draw_rect(&frame, 8, 8, width - 16, height - 16, ST7305_COLOR_BLACK));
    ESP_ERROR_CHECK(esp_rlcd_st7305_frame_buffer_fill_rect(&frame, 165, 115, 70, 70, ST7305_COLOR_BLACK));
    ESP_ERROR_CHECK(esp_rlcd_st7305_frame_buffer_flush(panel, &frame));
    esp_rlcd_st7305_frame_buffer_deinit(&frame);
}
