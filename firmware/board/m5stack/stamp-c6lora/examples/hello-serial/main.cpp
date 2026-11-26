// Adapted for EspHub from https://docs.m5stack.com/en/arduino/m5stamp_c6lora/program
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstdio>

static void setup() {}

static void loop() {
    puts("Hello, world!");
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
