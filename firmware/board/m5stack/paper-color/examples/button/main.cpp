// https://docs.m5stack.com/en/arduino/papercolor/button
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);

    auto& display = M5.Display;
    display.setEpdMode(epd_mode_t::epd_fast);
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextColor(BLACK);
    display.setTextDatum(middle_center);
    display.drawString("Ready: press A / B / C", display.width() / 2, display.height() / 2);
    display.endWrite();

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            std::puts("A was Pressed");
        }
        if (M5.BtnB.wasPressed()) {
            std::puts("B was Pressed");
        }
        if (M5.BtnC.wasPressed()) {
            std::puts("C was Pressed");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
