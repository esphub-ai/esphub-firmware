// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    int previous = -1;
    for (;;) {
        int level = gpio_get_level(GPIO_NUM_0);
        if (level != previous) ESP_LOGI("button", "BOOT %s", level ? "released" : "pressed");
        previous = level;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
