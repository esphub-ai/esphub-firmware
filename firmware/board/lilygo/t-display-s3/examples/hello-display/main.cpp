// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_15, 1); // Enable the display power rail.
    static lgfx::Panel_ST7789 panel;
    static lgfx::Bus_Parallel8 bus;
    auto pins = bus.config();
    pins.pin_wr = 8;
    pins.pin_rd = 9;
    pins.pin_rs = 7;
    const int data[] = {39, 40, 41, 42, 45, 46, 47, 48};
    for (int i = 0; i < 8; ++i) pins.pin_data[i] = data[i];
    bus.config(pins);
    example_draw_display(panel, bus, "EspHub", 6, 5, 38, 170, 320, 35);
}
