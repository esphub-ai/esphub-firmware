// Adapted for EspHub from https://docs.m5stack.com/en/arduino/m5air_quality/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

static void setup() {

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setTextSize(3);
  M5.Display.println("HelloWorld!");
  puts("Hello World!!!");
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
}
