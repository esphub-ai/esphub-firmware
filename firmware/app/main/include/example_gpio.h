// SPDX-License-Identifier: MIT
#pragma once

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void example_output(gpio_num_t pin, int level)
{
    ESP_ERROR_CHECK(gpio_set_level(pin, level));
    ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
}

static inline void example_blink(gpio_num_t pin, int on_level)
{
    example_output(pin, !on_level);
    for (;;) {
        ESP_ERROR_CHECK(gpio_set_level(pin, on_level));
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_ERROR_CHECK(gpio_set_level(pin, !on_level));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
