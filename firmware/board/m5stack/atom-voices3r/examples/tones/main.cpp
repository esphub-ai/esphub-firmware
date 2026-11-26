// Adapted for EspHub from https://docs.m5stack.com/en/arduino/atom_echos3r/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include "M5Unified.h"

static void setup() {
  M5.begin();
}

static void loop() {
  M5.Speaker.tone(7000, 100);  // frequency, duration
  vTaskDelay(pdMS_TO_TICKS(1000));
  M5.Speaker.tone(4000, 20);  // frequency, duration
  vTaskDelay(pdMS_TO_TICKS(1000));
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
