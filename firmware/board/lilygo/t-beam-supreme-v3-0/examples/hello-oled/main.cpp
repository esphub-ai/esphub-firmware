// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "LovyanGFX.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
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
