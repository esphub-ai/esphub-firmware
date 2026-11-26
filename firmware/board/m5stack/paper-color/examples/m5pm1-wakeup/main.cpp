// https://docs.m5stack.com/en/arduino/papercolor/wakeup#m5pm1-timer-wakeup
#include "eh_agent.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr uint8_t timerCounter = 0x38;
constexpr uint8_t timerConfig = 0x3c;
constexpr uint8_t timerKey = 0x3d;
constexpr uint8_t timerArm = 0x08;
constexpr uint8_t powerOn = 0x03;
constexpr uint8_t powerOff = 0x04;
constexpr uint8_t reloadTimer = 0xa5;
constexpr uint8_t timerWake = 0x01;
constexpr uint8_t seconds = 10;

void draw(const char* status, uint16_t color)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(top_left);
    display.setTextColor(BLACK);
    display.drawString("M5PM1 TIMER", 16, 24);
    display.setTextColor(BLUE);
    display.drawString("BtnA: power on after 10s", 16, 86);
    display.drawString("BtnB: power off after 10s", 16, 126);
    display.setTextColor(color);
    display.drawString(status, 16, display.height() - 40);
    display.endWrite();
}

bool armTimer(m5::M5PM1_Class& pm1, uint8_t action)
{
    // M5PM1 1.0.7 timerSet(): 31-bit seconds, selected action, then reload.
    const uint8_t count[] = {seconds, 0, 0, 0};
    return pm1.writeRegister(timerCounter, count, sizeof(count)) &&
           pm1.writeRegister8(timerConfig, timerArm | action) &&
           pm1.writeRegister8(timerKey, reloadTimer);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    auto& pm1 = M5.Power.M5pm1;
    if (!pm1.isEnabled()) {
        draw("PM1 init failed", RED);
        return;
    }
    auto& led = M5.Led;
    led.setAutoDisplay(false);
    const bool ledReady = led.getCount() == 2 && led.begin();
    if (ledReady) {
        led.setBrightness(255);
    }
    const uint8_t wake = pm1.getWakeSource();
    draw((wake & timerWake) ? "PM1 timer wake" : "PM1 ready", GREEN);
    std::printf("PM1 ready: reset=%d wake=0x%02x\n", static_cast<int>(esp_reset_reason()), wake);

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            std::puts("Button A: programming 10-second PM1 power-on");
            if (armTimer(pm1, powerOn)) {
                std::puts("PM1 timer armed");
                vTaskDelay(pdMS_TO_TICKS(1000));
                std::puts("PM1 shutdown requested");
                if (!pm1.powerOff()) {
                    draw("PM1 shutdown failed", RED);
                }
            } else {
                draw("PM1 timer setup failed", RED);
            }
        } else if (M5.BtnB.wasPressed()) {
            std::puts("Button B: programming 10-second PM1 power-off");
            const bool armed = armTimer(pm1, powerOff);
            if (ledReady) {
                const RGBColor color = armed ? RGBColor{0, 0, 60} : RGBColor{60, 0, 0};
                led.setColor(0, color);
                led.setColor(1, color);
                led.display();
            }
            if (armed) {
                draw("PM1 shutdown timer armed", BLUE);
                std::puts("PM1 shutdown timer armed");
            } else {
                draw("PM1 timer setup failed", RED);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
