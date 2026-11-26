// https://docs.m5stack.com/en/arduino/m5air_quality/wakeup
#include "eh_agent.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr int sleepSeconds = 5;

void show(const char* first, const char* second = nullptr)
{
    auto& display = M5.Display;
    const int x = display.width() / 2;
    const int y = display.height() / 2;
    display.startWrite();
    display.fillScreen(TFT_WHITE);
    display.setTextColor(TFT_BLACK);
    display.setTextSize(2);
    display.setTextDatum(middle_center);
    display.drawString(first, x, second ? y - 20 : y);
    if (second) {
        display.drawString(second, x, y + 20);
    }
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    const auto wake = esp_sleep_get_wakeup_cause();
    if (wake == ESP_SLEEP_WAKEUP_TIMER) {
        show("Woken up!", "Press BtnA");
    } else {
        show("Press BtnA", "to sleep 5s");
    }
    std::printf("AirQ wakeup ready: reset=%d wake=%d\n",
                static_cast<int>(esp_reset_reason()), static_cast<int>(wake));

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            show("Sleeping...");
            std::puts("Button A: requesting 5-second RTC sleep");
            vTaskDelay(pdMS_TO_TICKS(500));
            if (!M5.Power.timerSleep(sleepSeconds)) {
                show("Sleep failed", "Press BtnA");
                std::puts("RTC sleep request failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
