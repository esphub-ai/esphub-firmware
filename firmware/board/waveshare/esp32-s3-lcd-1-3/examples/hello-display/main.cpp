// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST7789 panel;
    example_spi(bus, 40, 41, 38);
    example_draw_display(panel, bus, "EspHub", 39, 42, -1, 240, 240, 0, 0, true);
}
