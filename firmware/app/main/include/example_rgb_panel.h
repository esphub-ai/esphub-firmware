// SPDX-License-Identifier: MIT
#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"

static inline void example_rgb_bars(const esp_lcd_rgb_panel_config_t *config)
{
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    int width = config->timings.h_res;
    int height = config->timings.v_res;
    uint16_t *row = heap_caps_malloc(width * sizeof(*row), MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(row ? ESP_OK : ESP_ERR_NO_MEM);
    const uint16_t colors[] = {0xf800, 0x07e0, 0x001f};
    for (int x = 0; x < width; ++x) row[x] = colors[x * 3 / width];
    for (int y = 0; y < height; ++y) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, width, y + 1, row));
    }
    free(row);
}
