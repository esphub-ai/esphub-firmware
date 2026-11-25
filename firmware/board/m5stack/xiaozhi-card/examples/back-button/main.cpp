// Adapted for EspHub from https://github.com/m5stack/XiaoZhi-Card/blob/44aa2f2b91c3e938932bb41f670e984cf752b731/main/boards/xiaozhi-card/config.h
#include "eh_agent.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    constexpr gpio_num_t button = GPIO_NUM_21;
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
