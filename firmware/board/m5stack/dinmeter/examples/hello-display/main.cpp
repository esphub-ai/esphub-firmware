// Adapted for EspHub from https://docs.m5stack.com/en/arduino/m5dinmeter/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

//––– Adjust these to position your text –––
static const int16_t X_POS = 0;   // horizontal pixel offset
static const int16_t Y_POS = 60;  // vertical pixel offset

static void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(X_POS, Y_POS);
  M5.Display.println("Hello World!");


  puts("Hello World!!!");
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
}
