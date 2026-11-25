// Adapted for EspHub from https://docs.m5stack.com/en/arduino/powerhub/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

static void setup() {
  M5.begin();
  M5.Led.setBrightness(127);
}

static void loop() {
  M5.update();

  M5.Led.setAllColor(TFT_WHITE);
  M5.Led.display();
  vTaskDelay(pdMS_TO_TICKS(500));

  M5.Led.setAllColor(TFT_BLACK);
  M5.Led.display();
  vTaskDelay(pdMS_TO_TICKS(500));
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
