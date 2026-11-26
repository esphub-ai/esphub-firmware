// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "LovyanGFX.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    static lgfx::Bus_I2C bus;
    static lgfx::Panel_SSD1306 panel;
    static lgfx::LGFX_Device oled;
    auto pins = bus.config();
    pins.i2c_port = 0;
    pins.pin_sda = 13;
    pins.pin_scl = 14;
    pins.i2c_addr = 0x3c;
    bus.config(pins);
    panel.setBus(&bus);
    oled.setPanel(&panel);
    ESP_ERROR_CHECK(oled.init() ? ESP_OK : ESP_FAIL);
    oled.fillScreen(0);
    oled.setTextColor(0xffffff);
    oled.setTextSize(2);
    oled.setCursor(20, 24);
    oled.print("EspHub");
    oled.display();
}
