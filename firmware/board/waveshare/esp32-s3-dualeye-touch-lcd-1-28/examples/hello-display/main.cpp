// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_38, 1); // Deselect the second panel on the shared SPI bus.
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_GC9A01 panel;
    example_spi(bus, 41, 42, 45);
    example_draw_display(panel, bus, "EspHub", 47, 48, 46, 240, 240, 0, 0, true);
}
