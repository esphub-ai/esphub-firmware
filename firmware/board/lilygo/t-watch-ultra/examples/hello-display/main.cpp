// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

#include "example_i2c.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto bus_i2c = example_i2c_bus(GPIO_NUM_3, GPIO_NUM_2);
    auto pmic = example_i2c_device(bus_i2c, 0x34);
    // AXP2101 ALDO2 supplies the display. XL9555 P07 enables it.
    example_update_bits(pmic, 0x93, 0x1f, 28); // 3.3 V
    example_update_bits(pmic, 0x90, 0x02, 0x02);
    auto expander = example_i2c_device(bus_i2c, 0x20);
    example_update_bits(expander, 0x02, 0x80, 0x80);
    example_update_bits(expander, 0x06, 0x80, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_CO5300 panel;
    example_qspi(bus, 40, 38, 39, 42, 45);
    example_draw_display(panel, bus, "EspHub", 41, -1, -1, 410, 502, 22, 0, false);
}
