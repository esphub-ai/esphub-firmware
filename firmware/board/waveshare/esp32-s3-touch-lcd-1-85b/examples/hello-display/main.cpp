// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST77916 panel;
    example_qspi(bus, 40, 46, 45, 42, 41);
    example_draw_display(panel, bus, "EspHub", 21, 3, 5, 360, 360, 0, 0, false);
}
