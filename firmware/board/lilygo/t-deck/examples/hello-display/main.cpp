// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_10, 1); // Peripheral power
    example_output(GPIO_NUM_39, 1); // SD chip select
    example_output(GPIO_NUM_9, 1);  // Radio chip select
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST7789 panel;
    example_spi(bus, 40, 41, 11);
    example_draw_display(panel, bus, "EspHub", 12, -1, 42, 240, 320, 0, 0, true);
}
