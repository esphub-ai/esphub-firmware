// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST7789 panel;
    example_spi(bus, 38, 39, 45);
    example_draw_display(panel, bus, "EspHub", 21, 40, 46, 172, 320, 34, 0, false);
}
