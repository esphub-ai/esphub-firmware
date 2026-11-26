// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_RM67162 panel;
    example_qspi(bus, 47, 18, 7, 48, 5);
    example_draw_display(panel, bus, "EspHub", 6, 17, -1, 536, 240, 0, 0, false);
}
