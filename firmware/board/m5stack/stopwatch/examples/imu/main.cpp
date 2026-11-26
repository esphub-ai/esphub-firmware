// Adapted from M5Stack's M5StopWatch-UserDemo/app_imu (MIT).
#include "../common/round_display.hpp"
#include "eh_agent.h"
#include <algorithm>
#include <cmath>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    if (!M5.Imu.isEnabled()) {
        centered("BMI270 unavailable", 230, 2);
        return;
    }
    const int cx = M5.Display.width() / 2, cy = M5.Display.height() / 2;
    const int radius = cx - 55;
    int last_x = cx, last_y = cy;
    centered("Tilt to move the ball", 55, 2);
    M5.Display.drawCircle(cx, cy, radius, TFT_DARKGREY);
    for (;;) {
        if (M5.Imu.update()) {
            const auto &imu = M5.Imu.getImuData();
            // Keep the entire ball inside the guide, including diagonal tilts.
            const float magnitude = std::max(1.0f, std::hypot(imu.accel.x, imu.accel.y));
            const int x = cx + std::lround(imu.accel.y / magnitude * (radius - 18));
            const int y = cy + std::lround(imu.accel.x / magnitude * (radius - 18));
            M5.Display.startWrite();
            M5.Display.fillCircle(last_x, last_y, 16, TFT_BLACK);
            M5.Display.fillCircle(x, y, 16, TFT_CYAN);
            M5.Display.endWrite();
            last_x = x;
            last_y = y;
        }
        M5.delay(20);
    }
}
