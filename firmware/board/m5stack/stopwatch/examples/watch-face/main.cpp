// Adapted from M5Stack's M5StopWatch-UserDemo/app_watch_face (MIT).
#include "../common/round_display.hpp"
#include "../common/rtc_clock.hpp"
#include "eh_agent.h"
#include <cmath>
#include <cstdio>

static void hand(float turns, int inner, int outer, uint16_t color)
{
    const float rad = turns * 6.283185307f;
    const float dx = std::sin(rad), dy = -std::cos(rad);
    const int x = M5.Display.width() / 2, y = M5.Display.height() / 2;
    M5.Display.drawLine(x + dx * inner, y + dy * inner, x + dx * outer, y + dy * outer, color);
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    RtcClock clock;
    bool analog = true;
    int previous_sec = -1;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
            analog = !analog;
            previous_sec = -1;
        }
        m5::rtc_datetime_t now{};
        if (!clock.read(now)) {
            centered("RTC unavailable", 230, 2);
            previous_sec = -1;
            M5.delay(250);
            continue;
        }
        const auto &t = now.time;
        if (t.seconds != previous_sec) {
            previous_sec = t.seconds;
            M5.Display.startWrite();
            M5.Display.clear();
            if (analog) {
                const int radius = M5.Display.width() / 2 - 18;
                for (int i = 0; i < 60; ++i)
                    hand(i / 60.f, radius - (i % 5 == 0 ? 16 : 5), radius, TFT_LIGHTGREY);
                hand((t.hours % 12 + t.minutes / 60.f) / 12.f, 0, radius * 0.48f, TFT_WHITE);
                hand((t.minutes + t.seconds / 60.f) / 60.f, 0, radius * 0.78f, TFT_CYAN);
                hand(t.seconds / 60.f, -16, radius * 0.86f, TFT_RED);
            } else {
                char text[32];
                snprintf(text, sizeof(text), "%02d:%02d:%02d", t.hours, t.minutes, t.seconds);
                centered(text, 214, 5);
                snprintf(text, sizeof(text), "%04d-%02d-%02d", now.date.year, now.date.month, now.date.date);
                centered(text, 278, 2, TFT_CYAN);
            }
            M5.Display.endWrite();
        }
        M5.delay(50);
    }
}
