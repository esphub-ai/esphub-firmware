// Adapted for EspHub from https://github.com/m5stack/STAMP-S3/blob/6b64d431f930ff5e2bd770a07003ac983c24bc3f/examples/Led/Led.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <M5Unified.h>
static constexpr uint32_t palette[] = {0xff0000, 0x00ff00, 0x0000ff};
static unsigned color;
static void setup() {
    M5.begin();
    M5.Led.setBrightness(32);
    M5.Led.setAllColor(palette[color]);
    M5.Led.display();
}
static void loop() {
    M5.update();
    if (M5.BtnA.wasPressed()) {
        color = (color + 1) % 3;
        M5.Led.setAllColor(palette[color]);
        M5.Led.display();
    }
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
