// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

#include "example_i2c.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto bus_i2c = example_i2c_bus(GPIO_NUM_10, GPIO_NUM_11);
    auto pmic = example_i2c_device(bus_i2c, 0x34);
    // AXP2101 ALDO2 powers the backlight and ALDO3 powers the panel.
    example_update_bits(pmic, 0x93, 0x1f, 28); // 3.3 V
    example_update_bits(pmic, 0x94, 0x1f, 28);
    example_update_bits(pmic, 0x90, 0x06, 0x06);
    vTaskDelay(pdMS_TO_TICKS(30));
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_ST7789 panel;
    example_spi(bus, 18, 13, 38);
    example_draw_display(panel, bus, "EspHub", 12, -1, 45, 240, 240, 0, 0, true);
}
