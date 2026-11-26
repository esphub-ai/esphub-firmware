// https://docs.m5stack.com/en/arduino/papercolor/rtc
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void drawClock(const m5::rtc_datetime_t& clock, int minute)
{
    auto& display = M5.Display;
    const int center = display.width() / 2;
    const int middle = display.height() / 2;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.drawString("PAPER COLOR RTC", center, 40);
    if (minute < 0) {
        display.setTextColor(RED);
        display.drawString("RTC unavailable", center, middle);
    } else {
        char text[32];
        std::snprintf(text, sizeof(text), "%02d:%02d", clock.time.hours, clock.time.minutes);
        display.setTextSize(3);
        display.setTextColor(RED);
        display.drawString(text, center, middle - 32);
        display.setTextSize(1);
        std::snprintf(text, sizeof(text), "%04d/%02d/%02d", clock.date.year,
                      clock.date.month, clock.date.date);
        display.setTextColor(BLUE);
        display.drawString(text, center, middle + 46);
        display.setTextColor(GREEN);
        display.drawString(weekdays[clock.date.weekDay], center, middle + 85);
    }
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);

    if (!M5.Rtc.setDateTime({{2026, 5, 8}, {12, 0, 0}})) {
        std::puts("RTC demo time could not be set");
        drawClock({}, -1);
        return;
    }
    std::puts("RTC demo time set to 2026/05/08 12:00:00");

    int displayedMinute = -2;
    for (;;) {
        M5.update();
        m5::rtc_datetime_t clock{};
        const bool valid = M5.Rtc.getDateTime(&clock);
        const int minute = valid ? clock.time.hours * 60 + clock.time.minutes : -1;
        if (minute != displayedMinute) {
            displayedMinute = minute;
            if (valid) {
                std::printf("RTC: %04d/%02d/%02d (%s) %02d:%02d:%02d\n",
                            clock.date.year, clock.date.month, clock.date.date,
                            weekdays[clock.date.weekDay], clock.time.hours,
                            clock.time.minutes, clock.time.seconds);
            } else {
                std::puts("RTC read failed");
            }
            drawClock(clock, minute);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
