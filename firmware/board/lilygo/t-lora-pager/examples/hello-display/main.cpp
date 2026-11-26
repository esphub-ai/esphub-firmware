// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_21, 1); // Deselect SD, radio and NFC on the shared bus.
    example_output(GPIO_NUM_36, 1);
    example_output(GPIO_NUM_39, 1);
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST7796 panel;
    example_spi(bus, 35, 34, 37);
    example_draw_display(panel, bus, "EspHub", 38, -1, 42, 222, 480, 49, 0, true);
}
