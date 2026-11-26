// https://docs.m5stack.com/en/arduino/m5air_quality/display
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstdlib>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

    auto& display = M5.Display;
    const int width = display.width();
    const int height = display.height();
    const int radius = (width >> 4) + 2;
    std::puts("AirQ display: drawing circles and rectangles");

    for (;;) {
        const int circleX = std::rand() % width;
        const int circleY = std::rand() % height;
        const auto circleColor = static_cast<uint16_t>(std::rand());
        const int rectX = std::rand() % width;
        const int rectY = std::rand() % height;
        const auto rectColor = static_cast<uint16_t>(std::rand());

        display.startWrite();
        display.fillCircle(circleX, circleY, radius, circleColor);
        display.fillRect(rectX - radius, rectY - radius, 2 * radius, 2 * radius, rectColor);
        display.endWrite();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
