// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_rgb_panel.h"
#include "example_i2c.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    i2c_master_bus_handle_t bus = example_i2c_bus(GPIO_NUM_8, GPIO_NUM_9);
    i2c_master_dev_handle_t expander = example_i2c_device(bus, 0x24);
    // IO2 enables the backlight, IO3 releases LCD reset, and IO5 selects USB when low.
    const uint8_t levels[] = {0x03, 0x0c};
    const uint8_t outputs[] = {0x02, 0x2c};
    ESP_ERROR_CHECK(i2c_master_transmit(expander, levels, sizeof(levels), 100));
    ESP_ERROR_CHECK(i2c_master_transmit(expander, outputs, sizeof(outputs), 100));
    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 30 * 1000 * 1000, .h_res = 1024, .v_res = 600,
            .hsync_pulse_width = 162, .hsync_back_porch = 152, .hsync_front_porch = 48,
            .vsync_pulse_width = 45, .vsync_back_porch = 13, .vsync_front_porch = 3,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16, .num_fbs = 1, .bounce_buffer_size_px = 1024 * 10,
        .hsync_gpio_num = 46, .vsync_gpio_num = 3,
        .de_gpio_num = 5, .pclk_gpio_num = 7, .disp_gpio_num = -1,
        .data_gpio_nums = { 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 },
        .flags.fb_in_psram = true,
    };
    example_rgb_bars(&config);
}
