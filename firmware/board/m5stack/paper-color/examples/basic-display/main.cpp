// Display layout from M5Stack's PaperColor Basic Display example:
// https://docs.m5stack.com/en/arduino/papercolor/display
#include "eh_agent.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
struct Letter {
    const char* text;
    uint16_t color;
};

constexpr Letter paper[] = {{"P", BLACK}, {"A", BLACK}, {"P", BLACK}, {"E", BLACK}, {"R", BLACK}};
constexpr Letter color[] = {{"C", RED}, {"O", YELLOW}, {"L", GREEN}, {"O", BLUE}, {"R", RED}};
constexpr uint16_t palette[] = {WHITE, BLACK, RED, YELLOW, GREEN, BLUE};

void drawHeading(M5Canvas& canvas, const Letter (&letters)[5], int y)
{
    int width = 0;
    for (const auto& letter : letters) {
        width += canvas.textWidth(letter.text);
    }
    int x = (canvas.width() - width) / 2;
    for (const auto& letter : letters) {
        canvas.setTextColor(letter.color);
        canvas.drawString(letter.text, x, y);
        x += canvas.textWidth(letter.text);
    }
}

void drawDisplay()
{
    const int width = M5.Display.width();
    const int height = M5.Display.height();
    M5Canvas canvas(&M5.Display);
    if (!canvas.createSprite(width, height)) {
        ESP_LOGE("basic-display", "Could not allocate %d x %d canvas", width, height);
        return;
    }

    canvas.fillSprite(WHITE);
    for (int inset = 0; inset <= 4; inset += 2) {
        canvas.drawRect(inset, inset, width - 2 * inset, height - 2 * inset, RED);
    }

    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextDatum(top_left);
    drawHeading(canvas, paper, 168);
    drawHeading(canvas, color, 236);

    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(BLACK);
    canvas.drawString("Panel: ED2208-DOA", width / 2, 336);
    char resolution[32];
    std::snprintf(resolution, sizeof(resolution), "Resolution: %d x %d", width, height);
    canvas.setTextColor(BLUE);
    canvas.drawString(resolution, width / 2, 376);
    canvas.setFont(&fonts::Font4);
    canvas.setTextColor(BLACK);
    canvas.drawString("4\" E-Paper E6 Full-Color", width / 2, 418);

    constexpr int margin = 46;
    const int cellWidth = (width - 2 * margin) / 6;
    for (int i = 0; i < 6; ++i) {
        const int left = margin + i * cellWidth;
        const int w = (i == 5) ? width - margin - left : cellWidth;
        canvas.fillRect(left, 448, w, 34, palette[i]);
        canvas.drawRect(left, 448, w, 34, BLACK);
    }
    canvas.pushSprite(0, 0);
}
}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(0);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    drawDisplay();

    for (;;) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
