// https://docs.m5stack.com/en/arduino/papercolor/wakeup
#include "eh_agent.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>
#include <cstdio>

namespace {
void draw(const char* status)
{
    auto& display = M5.Display;
    const int center = display.width() / 2;
    const int middle = display.height() / 2;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(RED);
    display.drawString("Press Top BtnA", center, middle - 20);
    display.setTextColor(GREEN);
    display.drawString(status, center, middle + 20);
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    draw("Sleep 5s, then wakeup");
    std::printf("RTC wakeup ready: reset=%d wake=%d\n", static_cast<int>(esp_reset_reason()),
                static_cast<int>(esp_sleep_get_wakeup_cause()));

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            std::puts("Button A: requesting 5-second RTC sleep");
            if (!M5.Power.timerSleep(5)) {
                draw("Sleep request failed");
                std::puts("RTC sleep request failed");
            }
        }
        vTaskDelay(1);
    }
}
