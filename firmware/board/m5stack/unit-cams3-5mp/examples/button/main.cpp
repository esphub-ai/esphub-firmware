// Adapted for EspHub from https://github.com/m5stack/UnitCamS3-UserDemo/blob/50410249ece3f92f414a222c859dd48d5958fd97/platforms/unitcam_s3_5mp/main/hal_unitcam_s3_5mp/components/hal_gamepad.cpp
#include "eh_agent.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    constexpr gpio_num_t button = GPIO_NUM_0;
    ESP_ERROR_CHECK(gpio_set_direction(button, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(button, GPIO_FLOATING));
    int previous = gpio_get_level(button);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const int level = gpio_get_level(button);
        if (level != previous) {
            puts(level ? "Button released" : "Button pressed");
            previous = level;
        }
    }
}
