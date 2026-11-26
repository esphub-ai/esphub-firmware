// https://docs.m5stack.com/en/arduino/papercolor/speaker
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);

    auto& display = M5.Display;
    display.setRotation(1);
    display.setEpdMode(epd_mode_t::epd_fast);
    display.startWrite();
    display.fillScreen(WHITE);
    display.setTextColor(RED);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::Font4);
    display.setTextSize(2);
    display.drawString("Speaker Tone Test", display.width() / 2, display.height() / 2);
    display.endWrite();

    for (;;) {
        M5.Speaker.tone(4000, 200);
        vTaskDelay(pdMS_TO_TICKS(2000));
        M5.Speaker.tone(8000, 200);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
