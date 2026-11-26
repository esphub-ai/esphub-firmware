// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_GC9A01 panel;
    example_spi(bus, 10, 11, 8);
    example_draw_display(panel, bus, "EspHub", 9, 12, 40, 240, 240, 0, 0, true);
}
