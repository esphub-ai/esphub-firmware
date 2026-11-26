// https://docs.m5stack.com/en/arduino/m5air_quality/button
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.clear();
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 8);
    M5.Display.println("M5AirQ");
    M5.Display.println("Button Test");
    std::puts("M5AirQ Button Test");

    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            std::puts("AirQ BtnA clicked");
            M5.Display.println("BtnA clicked");
        }
        if (M5.BtnB.wasClicked()) {
            std::puts("AirQ BtnB clicked");
            M5.Display.println("BtnB clicked");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
