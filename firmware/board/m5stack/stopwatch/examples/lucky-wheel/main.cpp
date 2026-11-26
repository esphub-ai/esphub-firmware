// Adapted from M5Stack's M5StopWatch-UserDemo/app_lucky_wheel (MIT).
#include "../common/round_display.hpp"
#include "eh_agent.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static void draw_wheel(int sectors)
{
    auto &d = M5.Display;
    const int x = d.width() / 2, y = d.height() / 2, radius = x - 12;
    constexpr uint16_t colors[] = {TFT_DARKGREEN, TFT_NAVY, TFT_MAROON, TFT_PURPLE, TFT_DARKCYAN};
    d.clear();
    d.startWrite();
    for (int i = 0; i < sectors; ++i) {
        const float start = i * 360.f / sectors;
        const float end = (i + 1) * 360.f / sectors;
        d.fillArc(x, y, radius, radius / 2, start, end, colors[i % 5]);
        const float angle = (start + end) * 0.5f * 0.017453293f;
        char label[12];
        snprintf(label, sizeof(label), "%d", i + 1);
        d.setTextSize(3);
        d.setTextDatum(lgfx::textdatum_t::middle_center);
        d.setTextColor(TFT_WHITE);
        d.drawString(label, x + radius * 0.78f * std::cos(angle), y + radius * 0.78f * std::sin(angle));
    }
    d.endWrite();
}

static void pointer(float degrees)
{
    auto &d = M5.Display;
    const int x = d.width() / 2, y = d.height() / 2, length = (x - 12) / 2 - 2;
    const float a = degrees * 0.017453293f;
    d.fillCircle(x, y, length, TFT_BLACK);
    d.fillTriangle(x - 9 * std::sin(a), y + 9 * std::cos(a),
                   x + length * std::cos(a), y + length * std::sin(a),
                   x + 9 * std::sin(a), y - 9 * std::cos(a), TFT_YELLOW);
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    int sectors = 6;
    float angle = 0;
    for (;;) {
        M5.Display.clear();
        centered("Number of options", 150, 3);
        centered("A: fewer    B: more", 280, 2);
        centered("Tap to confirm", 318, 2, TFT_CYAN);
        for (;;) {
            char number[8];
            snprintf(number, sizeof(number), "%2d", sectors);
            centered(number, 210, 5);
            M5.update();
            if (M5.BtnA.wasPressed()) sectors = std::max(2, sectors - 1);
            if (M5.BtnB.wasPressed()) sectors = std::min(18, sectors + 1);
            if (M5.Touch.getDetail().wasClicked()) break;
            M5.delay(20);
        }
        draw_wheel(sectors);
        pointer(angle);
        for (;;) {
            M5.update();
            const auto touch = M5.Touch.getDetail();
            if (touch.wasHold()) break;
            if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || touch.wasClicked()) {
                const int chosen = esp_random() % sectors;
                const float target = (chosen + 0.5f) * 360.f / sectors;
                const float clockwise = std::fmod(target - angle + 360.f, 360.f);
                const float travel = clockwise + (M5.BtnA.wasPressed() ? -5.f : 4.f) * 360.f;
                const int64_t start = esp_timer_get_time();
                float progress;
                do {
                    progress = std::min(1.f, (esp_timer_get_time() - start) / 2800000.f);
                    pointer(angle + travel * (1.f - std::pow(1.f - progress, 3.f)));
                    M5.delay(25);
                } while (progress < 1.f);
                angle = target;
            }
            M5.delay(15);
        }
    }
}
