// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_i2c.h"
#include "LovyanGFX.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    // ALDO1 supplies the OLED and its I2C pull-ups on the V3.0 carrier.
    auto power_bus = example_i2c_bus(GPIO_NUM_42, GPIO_NUM_41);
    auto axp2101 = example_i2c_device(power_bus, 0x34);
    example_update_bits(axp2101, 0x92, 0x1f, 28); // 3.3 V
    example_update_bits(axp2101, 0x90, 0x01, 0x01);
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(axp2101));
    ESP_ERROR_CHECK(i2c_del_master_bus(power_bus));
    vTaskDelay(pdMS_TO_TICKS(30));

    static lgfx::Bus_I2C bus;
    static lgfx::Panel_SH110x panel;
    static lgfx::LGFX_Device oled;
    auto pins = bus.config();
    pins.i2c_port = 0;
    pins.pin_sda = 17;
    pins.pin_scl = 18;
    pins.i2c_addr = 0x3c;
    bus.config(pins);
    auto geometry = panel.config();
    geometry.memory_width = 132;
    geometry.panel_width = 128;
    geometry.memory_height = geometry.panel_height = 64;
    geometry.offset_x = 2;
    panel.config(geometry);
    panel.setBus(&bus);
    oled.setPanel(&panel);
    ESP_ERROR_CHECK(oled.init() ? ESP_OK : ESP_FAIL);
    // The SH1106 panel has 64 COM lines.
    oled.startWrite();
    oled.writeCommand(0xa8);
    oled.writeCommand(0x3f);
    oled.endWrite();
    oled.fillScreen(0);
    oled.setTextColor(0xffffff);
    oled.setTextSize(2);
    oled.setCursor(20, 24);
    oled.print("EspHub");
    oled.display();
}
