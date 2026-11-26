// SPDX-License-Identifier: MIT
#pragma once

#include <LovyanGFX.hpp>
#include "example_gpio.h"

// Board entrypoints provide pin numbers, controller geometry and visible offsets.
template <class Panel, class Bus>
inline void example_draw_display(Panel &panel, Bus &bus, const char *message,
                                 int cs, int reset, int backlight, int width, int height,
                                 int offset_x = 0, int offset_y = 0, bool invert = true)
{
    static lgfx::LGFX_Device display;
    auto config = panel.config();
    config.pin_cs = cs;
    config.pin_rst = reset;
    config.panel_width = width;
    config.panel_height = height;
    config.memory_width = width + 2 * offset_x;
    config.memory_height = height + 2 * offset_y;
    config.offset_x = offset_x;
    config.offset_y = offset_y;
    config.readable = false;
    config.invert = invert;
    panel.config(config);
    panel.setBus(&bus);
    display.setPanel(&panel);
    ESP_ERROR_CHECK(display.init() ? ESP_OK : ESP_FAIL);
    if (backlight >= 0) example_output(static_cast<gpio_num_t>(backlight), 1);
    display.setBrightness(96);
    const uint32_t colors[] = {0x963939, 0x25634b, 0x284c84};
    for (int i = 0; i < 3; ++i) {
        int top = i * display.height() / 3;
        int bottom = (i + 1) * display.height() / 3;
        display.fillRect(0, top, display.width(), bottom - top, colors[i]);
    }
    display.setTextColor(0xffffff);
    display.setTextDatum(lgfx::textdatum_t::middle_center);
    display.setTextSize(2);
    display.drawString(message, display.width() / 2, display.height() / 2);
}

inline void example_spi(lgfx::Bus_SPI &bus, int clock, int mosi, int dc)
{
    auto config = bus.config();
    config.spi_host = SPI2_HOST;
    config.freq_write = 20000000;
    config.pin_sclk = clock;
    config.pin_mosi = mosi;
    config.pin_miso = -1;
    config.pin_dc = dc;
    bus.config(config);
}

inline void example_qspi(lgfx::Bus_SPI &bus, int clock, int d0, int d1, int d2, int d3)
{
    auto config = bus.config();
    config.spi_host = SPI2_HOST;
    config.freq_write = 20000000;
    config.pin_sclk = clock;
    config.pin_io0 = d0;
    config.pin_io1 = d1;
    config.pin_io2 = d2;
    config.pin_io3 = d3;
    bus.config(config);
}
