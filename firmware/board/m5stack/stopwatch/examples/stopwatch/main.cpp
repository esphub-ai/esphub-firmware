// Adapted from M5Stack's M5StopWatch-UserDemo/app_stopwatch (MIT).
#include "../common/round_display.hpp"
#include "eh_agent.h"
#include "esp_timer.h"
#include <array>
#include <cstdio>

static void format_time(char *text, size_t size, int64_t us)
{
    const int64_t cs = us / 10000;
    snprintf(text, size, "%02lld:%02lld:%02lld.%02lld", cs / 360000,
             cs / 6000 % 60, cs / 100 % 60, cs % 100);
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    bool running = false;
    int64_t started = 0, accumulated = 0;
    std::array<int64_t, 4> laps{};
    unsigned lap_count = 0;
    bool redraw = true;
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (M5.BtnB.wasPressed()) {
            if (running) accumulated += now - started;
            else started = now;
            running = !running;
            redraw = true;
        }
        const int64_t elapsed = accumulated + (running ? now - started : 0);
        if (M5.BtnA.wasPressed()) {
            if (running) {
                laps[lap_count % laps.size()] = elapsed;
                ++lap_count;
            } else {
                accumulated = 0;
                lap_count = 0;
            }
            redraw = true;
        }
        char value[32];
        if (redraw) {
            M5.Display.clear();
            centered(running ? "A: LAP    B: PAUSE" : "A: RESET    B: START", 105, 2);
            for (unsigned i = 0; i < laps.size() && i < lap_count; ++i) {
                const unsigned n = lap_count - i;
                format_time(value, sizeof(value), laps[(n - 1) % laps.size()]);
                char row[64];
                snprintf(row, sizeof(row), "Lap %u   %s", n, value);
                centered(row, 255 + 26 * i, 2, TFT_CYAN);
            }
            redraw = false;
        }
        format_time(value, sizeof(value), running ? elapsed : accumulated);
        centered(value, 184, 4);
        M5.delay(20);
    }
}
