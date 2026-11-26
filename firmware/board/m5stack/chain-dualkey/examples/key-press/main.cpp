// Adapted for EspHub from https://docs.m5stack.com/en/arduino/chain_dualkey/program
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
    ESP_ERROR_CHECK(gpio_set_pull_mode(button, GPIO_PULLUP_ONLY));
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
