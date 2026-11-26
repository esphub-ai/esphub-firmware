// https://docs.m5stack.com/en/arduino/papercolor/rgb_led
#include "eh_agent.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>

namespace {
constexpr uint8_t intensity = 80;
constexpr RGBColor off{0, 0, 0};
constexpr RGBColor white{intensity, intensity, intensity};
constexpr RGBColor red{intensity, 0, 0};
constexpr RGBColor green{0, intensity, 0};
constexpr RGBColor blue{0, 0, intensity};

void showPair(RGBColor first, RGBColor second)
{
    M5.Led.setColor(0, first);
    M5.Led.setColor(1, second);
    M5.Led.display();
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);

    // M5Unified's Paper Color setup enables the M5PM1 LED rail and selects GPIO21.
    auto& led = M5.Led;
    led.setAutoDisplay(false);
    const bool ready = led.getCount() == 2 && led.begin();
    if (ready) {
        // RGB values already match the upstream strip's linear brightness of 80.
        led.setBrightness(255);
        showPair(red, red);
    } else {
        ESP_LOGE("rgb-led", "Paper Color RGB LED strip unavailable");
    }

    auto& display = M5.Display;
    display.setEpdMode(epd_mode_t::epd_fast);
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextColor(BLACK);
    display.setTextDatum(middle_center);
    display.drawString("RGB LED TEST", display.width() / 2, display.height() / 2);
    display.endWrite();

    constexpr RGBColor sequence[][2] = {
        {white, off}, {off, white}, {red, blue}, {blue, red},
        {red, red}, {green, green}, {blue, blue},
    };
    for (;;) {
        for (const auto& colors : sequence) {
            M5.update();
            if (ready) {
                showPair(colors[0], colors[1]);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
