// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_18, 1); // Hold the board power rail on.
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_CO5300 panel;
    example_qspi(bus, 11, 12, 13, 14, 15);
    example_draw_display(panel, bus, "EspHub", 10, 8, -1, 466, 466, 6, 0, false);
}
