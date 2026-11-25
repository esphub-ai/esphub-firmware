// Adapted for EspHub from https://docs.m5stack.com/en/arduino/corematrix/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

static const uint16_t HEART[16] = {
  0b0000000000000000,
  0b0000011001100000,
  0b0001111111111000,
  0b0011111111111100,
  0b0111111111111110,
  0b0111111111111110,
  0b0111111111111110,
  0b0011111111111100,
  0b0001111111111000,
  0b0000111111110000,
  0b0000011111100000,
  0b0000001111000000,
  0b0000000110000000,
  0b0000000000000000,
  0b0000000000000000,
  0b0000000000000000,
};

static void drawHeart()
{
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);

  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      if (HEART[y] & (0x8000u >> x)) {
        M5.Display.drawPixel(x, y, TFT_WHITE);
      }
    }
  }

  M5.Display.endWrite();
  M5.Display.display();
}

static void setup()
{
  auto cfg = M5.config();

  M5.begin(cfg);

  M5.Display.setColorDepth(8);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);

  printf("CoreMatrix display: %d x %d\n",
                static_cast<int>(M5.Display.width()), static_cast<int>(M5.Display.height()));
  drawHeart();
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
