// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "hub75.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    Hub75Config config{};
    config.panel_width = 64;
    config.panel_height = 64;
    config.layout_rows = 1;
    config.layout_cols = 1;
    config.brightness = 32;
    config.pins.r1 = 4;   config.pins.g1 = 5;   config.pins.b1 = 6;
    config.pins.r2 = 7;   config.pins.g2 = 15;  config.pins.b2 = 16;
    config.pins.a = 18;   config.pins.b = 8;    config.pins.c = 3;
    config.pins.d = 42;   config.pins.e = 9;
    config.pins.lat = 40; config.pins.oe = 2;   config.pins.clk = 41;
    static Hub75Driver display(config);
    ESP_ERROR_CHECK(display.begin() ? ESP_OK : ESP_FAIL);
    uint16_t row[64];
    for (unsigned y = 0; y < 64; ++y) {
        for (auto &pixel : row) pixel = y < 21 ? 0xf800 : y < 43 ? 0x07e0 : 0x001f;
        display.draw_pixels(0, y, 64, 1, (const uint8_t *)row, Hub75PixelFormat::RGB565,
                            Hub75ColorOrder::RGB, false);
    }
    display.flip_buffer();
}
