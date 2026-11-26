// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_SH8601Z panel;
    example_qspi(bus, 11, 4, 5, 6, 7);
    example_draw_display(panel, bus, "EspHub", 12, -1, -1, 368, 448, 0, 0, false);
}
