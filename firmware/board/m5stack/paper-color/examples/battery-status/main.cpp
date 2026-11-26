// https://docs.m5stack.com/en/arduino/papercolor/battery
#include "eh_agent.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr int64_t refreshInterval = 60 * 1000000;
constexpr int vbusThreshold = 1000;
constexpr uint8_t ledIntensity = 80;

void showPower(int vbus, bool ledsReady)
{
    const bool externalPower = vbus > vbusThreshold;
    const int level = M5.Power.getBatteryLevel();
    const int battery = M5.Power.getBatteryVoltage();
    auto& display = M5.Display;
    const int center = display.width() / 2;
    char line[48];

    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString("Power Status", center, 80);
    display.drawString("Refresh: 1 min", center, 125);
    display.setTextColor(externalPower ? GREEN : RED);
    display.drawString(externalPower ? "Input Power: ON" : "Input Power: OFF", center, 205);
    display.setTextColor(BLACK);
    std::snprintf(line, sizeof(line), "Battery: %d%%", level);
    display.drawString(line, center, 275);
    std::snprintf(line, sizeof(line), "Voltage: %d mV", battery);
    display.drawString(line, center, 325);
    if (vbus >= 0) {
        std::snprintf(line, sizeof(line), "VBUS: %d mV", vbus);
    } else {
        std::snprintf(line, sizeof(line), "VBUS: N/A");
    }
    display.drawString(line, center, 375);
    display.endWrite();

    if (ledsReady) {
        M5.Led.setAllColor(0, externalPower ? ledIntensity : 0, externalPower ? 0 : ledIntensity);
        M5.Led.display();
    }
    std::printf("Power info refreshed: input=%d, level=%d%%, bat=%dmV, vbus=%dmV\n",
                externalPower, level, battery, vbus);
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    auto& led = M5.Led;
    led.setAutoDisplay(false);
    const bool ledsReady = led.getCount() == 2 && led.begin();
    if (ledsReady) {
        led.setBrightness(255);
    } else {
        ESP_LOGE("battery-status", "Paper Color RGB LED strip unavailable");
    }

    int vbus = M5.Power.getVBUSVoltage();
    bool externalPower = vbus > vbusThreshold;
    showPower(vbus, ledsReady);
    int64_t lastRefresh = esp_timer_get_time();
    for (;;) {
        M5.update();
        vbus = M5.Power.getVBUSVoltage();
        const bool inputNow = vbus > vbusThreshold;
        if (inputNow != externalPower || esp_timer_get_time() - lastRefresh >= refreshInterval) {
            externalPower = inputNow;
            showPower(vbus, ledsReady);
            lastRefresh = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
