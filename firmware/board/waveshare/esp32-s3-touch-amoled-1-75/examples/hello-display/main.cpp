// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_CO5300 panel;
    example_qspi(bus, 38, 4, 5, 6, 7);
    example_draw_display(panel, bus, "EspHub", 12, 39, -1, 466, 466, 6, 0, false);
}
