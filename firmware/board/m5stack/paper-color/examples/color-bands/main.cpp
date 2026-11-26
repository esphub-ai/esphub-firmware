// Adapted for EspHub from https://docs.m5stack.com/en/arduino/papercolor/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

M5Canvas Canvas(&M5.Display);

static void drawBoldText(M5Canvas& canvas, const char* text, int x, int y, uint16_t color)
{
    // Draw the text in small offsets to emulate a bold weight on EPD.
    canvas.setTextColor(color);
    canvas.drawString(text, x, y);
    canvas.drawString(text, x + 1, y);
    canvas.drawString(text, x, y + 1);
    canvas.drawString(text, x + 1, y + 1);
}

static void setup()
{
    auto cfg          = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    const int screen_w = M5.Display.width();
    const int screen_h = M5.Display.height();

    Canvas.createSprite(screen_w, screen_h);
    Canvas.fillSprite(WHITE);

    const uint16_t band_colors[6] = {YELLOW, RED, GREEN, BLUE, BLACK, WHITE};
    const int band_h             = screen_h / 6;

    for (int i = 0; i < 6; ++i)
    {
        const int y = i * band_h;
        const int h = (i == 5) ? (screen_h - y) : band_h;
        Canvas.fillRect(0, y, screen_w, h, band_colors[i]);
    }

    const int white_band_y = 5 * band_h;
    const int white_band_h = screen_h - white_band_y;

    Canvas.setTextFont(4);
    Canvas.setTextSize(2);
    Canvas.setTextDatum(middle_left);
    Canvas.setTextColor(BLACK);

    const char text_paper[] = "PAPER";
    const char text_color[] = "COLOR";
    const int gap_w         = Canvas.textWidth("   ");
    const int total_w       = Canvas.textWidth(text_paper) + gap_w + Canvas.textWidth(text_color);
    const int start_x       = (screen_w - total_w) / 2;
    const int text_y        = white_band_y + (white_band_h / 2);

    drawBoldText(Canvas, text_paper, start_x, text_y, BLACK);

    int x = start_x + Canvas.textWidth(text_paper) + gap_w;
    const uint16_t color_text_colors[5] = {RED, YELLOW, GREEN, YELLOW, BLUE};
    for (int i = 0; text_color[i]; ++i)
    {
        const char ch[] = {text_color[i], 0};
        drawBoldText(Canvas, ch, x, text_y, color_text_colors[i]);
        x += Canvas.textWidth(ch);
    }

    Canvas.pushSprite(0, 0);
}

static void loop()
{
    M5.update();
    vTaskDelay(pdMS_TO_TICKS(100));
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
    for (;;) {
        loop();
        vTaskDelay(1);
    }
}
