// https://docs.m5stack.com/en/arduino/papercolor/sht40
#include "eh_agent.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include "../sht4x.hpp"
#include <cstdio>

namespace {
constexpr int64_t updateInterval = 120 * 1000000;

void showSample(bool valid, float temperature, float humidity)
{
    auto& display = M5.Display;
    const int x = display.width() / 2;
    const int y = display.height() / 2;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString("SHT40 Sensor", x, y - 105);

    char text[40];
    if (valid) {
        std::snprintf(text, sizeof(text), "Temp: %.1f C", temperature);
        display.setTextColor(RED);
        display.drawString(text, x, y - 22);
        std::snprintf(text, sizeof(text), "Humi: %.1f %%", humidity);
        display.setTextColor(BLUE);
        display.drawString(text, x, y + 34);
    } else {
        display.setTextColor(RED);
        display.drawString("SHT40 reading unavailable", x, y);
    }
    display.endWrite();
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setEpdMode(epd_mode_t::epd_fast);

    int64_t nextSample = esp_timer_get_time();
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (now >= nextSample) {
            nextSample = now + updateInterval;
            float temperature = 0;
            float humidity = 0;
            const bool valid = paper_color::sht4x::sample(temperature, humidity);
            showSample(valid, temperature, humidity);
            if (valid) {
                std::printf("SHT40: %.1f C, %.1f %% at %lld us\n", temperature, humidity,
                            static_cast<long long>(now));
            } else {
                std::puts("SHT40 reading unavailable");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
