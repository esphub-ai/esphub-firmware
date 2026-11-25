// Adapted for EspHub from https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/examples/Basic/HowToUse/HowToUse.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>

static void setup() {
    M5.begin();
    M5.Display.setTextSize(2);
    M5.Display.println("vameter ready");
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
}
