// SPDX-License-Identifier: MIT
#pragma once

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Blink all pixels together at low current. White works with GRB and RGB strips.
static inline void example_pixel_blink(int data_pin, unsigned count)
{
    led_strip_config_t pixels = {
        .strip_gpio_num = data_pin,
        .max_leds = count,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
    };
    led_strip_handle_t strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&pixels, &rmt, &strip));
    for (;;) {
        for (unsigned i = 0; i < count; ++i) {
            ESP_ERROR_CHECK(led_strip_set_pixel(strip, i, 16, 16, 16));
        }
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_ERROR_CHECK(led_strip_clear(strip));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
