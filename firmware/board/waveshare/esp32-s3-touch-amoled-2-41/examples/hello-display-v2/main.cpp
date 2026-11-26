// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"

#include "example_i2c.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto i2c = example_i2c_bus(GPIO_NUM_47, GPIO_NUM_48);
    auto expander = example_i2c_device(i2c, 0x20);
    // V2 connects panel reset to bit 0 of the TCA9554.
    example_update_bits(expander, 0x01, 0x01, 0);
    example_update_bits(expander, 0x03, 0x01, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    example_update_bits(expander, 0x01, 0x01, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    static lgfx::Bus_SPI bus;
    static lgfx::Panel_RM690B0 panel;
    example_qspi(bus, 10, 11, 12, 13, 14);
    example_draw_display(panel, bus, "EspHub", 9, -1, -1, 450, 600, 16, 0, false);
}
