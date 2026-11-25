// Adapted for EspHub from https://github.com/m5stack/M5GFX/blob/28d47949bdc84f30881623476f555398e2194313/examples/Basic/GameOfLife/GameOfLife.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>
#include "esp_random.h"
#include <cmath>
static auto &display = M5.Display;






M5Canvas canvas[2];

static void setup(void)
{
  M5.begin();
  display.setColorDepth(8);
  display.setEpdMode(epd_mode_t::epd_fastest);

  if (display.width() < display.height())
  {
    display.setRotation(display.getRotation() ^ 1);
    display.setPivot(display.width() /2 -0.5, display.height() /2 - 0.5);
  }

  int z = 0;
  int width;
  do
  {
    width = display.width() / ++z;
  } while (width > 192);

  z = 0;
  int height;
  do
  {
    height = display.height() / ++z;
  } while (height > 160);

  for (int i = 0; i < 2; i++)
  {
    canvas[i].setColorDepth(8);
    canvas[i].createSprite(width, height);
    canvas[i].createPalette();
    canvas[i].setPaletteColor(1, TFT_WHITE);
    canvas[i].setPivot(canvas[i].width() /2 -0.5, canvas[i].height() /2 - 0.5);
  }
  canvas[0].setTextColor(1);
  canvas[0].setTextDatum(textdatum_t::bottom_center);
  canvas[0].drawString("Conway's", canvas[0].width() >> 1, canvas[0].height() >> 1);
  canvas[0].setTextDatum(textdatum_t::top_center);
  canvas[0].drawString("Game of Life", canvas[0].width() >> 1, canvas[0].height() >> 1);
  canvas[0].pushRotateZoom(&display, 0,  (float)display.width() / canvas[0].width(), (float)display.height() / canvas[0].height());
  vTaskDelay(pdMS_TO_TICKS(2000));
  display.clear();
}

static void loop(void)
{
  bool flip = false;
  int width = canvas[flip].width();
  int height = canvas[flip].height();
  int xz = display.width() / width;
  int yz = display.height() / height;

  int y = 1;
  do
  {
    int x = 1;
    do
    {
      if ((esp_random() % 6U) == 0) { canvas[flip].drawPixel(x, y, 1); }
    } while (++x < width - 1);
  } while (++y < height - 1);

  int diff;
  display.startWrite();
  do
  {
    flip = !flip;
    diff = 0;

    auto old_buf = (uint8_t*)canvas[!flip].getBuffer();
    auto new_buf = (uint8_t*)canvas[ flip].getBuffer();
    int width  = canvas[flip].width();
    int height = canvas[flip].height();
    int py;
    int y  = height - 1;
    int ny = 0;
    do
    {
      py = y;
      y = ny;
      if (++ny == height) ny = 0;

      int px;
      int x  = width - 1;
      int nx = 0;
      do
      {
        px = x;
        x = nx;
        if (++nx == width) nx = 0;

        int neighbors = old_buf[px + py * width]
                      + old_buf[ x + py * width]
                      + old_buf[nx + py * width]
                      + old_buf[px +  y * width]
                      + old_buf[nx +  y * width]
                      + old_buf[px + ny * width]
                      + old_buf[ x + ny * width]
                      + old_buf[nx + ny * width];
        int idx = x + y * width;
        bool flg = (neighbors == 3) || (neighbors == 2 && old_buf[idx]);
        if (flg != new_buf[idx])
        {
          display.fillRect(x * xz, y * yz, xz, yz, flg ? TFT_WHITE : TFT_BLACK);
          new_buf[idx] = flg;
          ++diff;
        }
      } while (nx);
    } while (ny);

    display.display();
    // canvas[flip].pushRotateZoom(&display, 0,  (float)display.width() / width, (float)display.height() / height);
  } while (diff);
  display.endWrite();
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
