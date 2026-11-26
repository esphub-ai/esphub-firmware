// https://docs.m5stack.com/en/arduino/papercolor/m5pm1
#include "eh_agent.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>
#include <cstdio>

namespace {
using PM1 = m5::M5PM1_Class;
constexpr uint8_t holdConfig = 0x07;
constexpr uint8_t gpioFunctions = 0x16;
constexpr uint8_t gpioWakeEnable = 0x18;
constexpr uint8_t gpioWakeEdge = 0x19;
constexpr uint8_t gpio0 = 1 << 0;
constexpr uint8_t gpio2 = 1 << 2;
constexpr uint8_t ldoHold = 1 << 5;
constexpr uint8_t externalWake = 0x20;
constexpr uint8_t gpio2FunctionMask = 0x30;
constexpr uint8_t gpio2WakeFunction = 0x20;

void draw(const char* status)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(top_left);
    display.setTextColor(BLACK);
    display.drawString("RTC Wakeup Test", 32, 68);
    display.drawString(status, 32, 124);
    display.endWrite();
}

bool configureWake(PM1& pm1)
{
    uint8_t enabled = 0;
    uint8_t functions = 0;
    // GPIO0 and GPIO2 share a wake input. Preserve any existing GPIO0 wake.
    return pm1.readRegister(gpioWakeEnable, &enabled, 1) && !(enabled & gpio0) &&
           pm1.readRegister(gpioFunctions, &functions, 1) &&
           // M5Unified lacks the WAKE mux value exposed by M5PM1 1.0.7.
           pm1.writeRegister8(gpioFunctions, (functions & ~gpio2FunctionMask) | gpio2WakeFunction) &&
           pm1.setGPIOPull(PM1::gpio2, PM1::pull_up) &&
           pm1.bitOff(gpioWakeEdge, gpio2) &&
           pm1.bitOn(gpioWakeEnable, gpio2) &&
           pm1.setLDOOutput(true) &&
           pm1.bitOn(holdConfig, ldoHold) &&
           pm1.setLedEnLevel(true);
}

bool clearRtcTimer()
{
    const bool disabled = M5.Rtc.disableIRQ();
    const bool cleared = M5.Rtc.clearIRQ();
    return disabled && cleared;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);

    auto& pm1 = M5.Power.M5pm1;
    if (!pm1.isEnabled() || !M5.Rtc.isEnabled() || !clearRtcTimer()) {
        draw("RTC or PM1 unavailable");
        return;
    }
    const uint8_t wake = pm1.getWakeSource();
    draw(wake & externalWake ? "RTC wake: PM1 GPIO2" : "Press BtnA to Sleep");
    std::printf("RTC PM1 ready: reset=%d wake=0x%02x\n", static_cast<int>(esp_reset_reason()), wake);

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            std::puts("Button A: requesting RTC PM1 power cycle");
            if (configureWake(pm1) && clearRtcTimer()) {
                draw("Power OFF - wake in 5s");
                if (M5.Rtc.setTimerIRQ(5000)) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    std::puts("RTC armed: requesting PM1 shutdown");
                    if (pm1.powerOff()) {
                        return;
                    }
                }
                clearRtcTimer();
            }
            draw("RTC power cycle failed");
        }
        vTaskDelay(1);
    }
}
