// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_display.hpp"
#include "esp_rom_sys.h"

// The manufacturer reads ID1 (0xda) using opcode 0x03 and a 24-bit address.
static uint8_t panel_id()
{
    constexpr auto cs = GPIO_NUM_9, clock = GPIO_NUM_10, data = GPIO_NUM_11, reset = GPIO_NUM_21;
    example_output(cs, 1);
    example_output(clock, 0);
    example_output(data, 0);
    example_output(reset, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(gpio_set_level(reset, 1));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(gpio_set_level(cs, 0));
    const uint32_t command = 0x0300da00;
    for (int bit = 31; bit >= 0; --bit) {
        ESP_ERROR_CHECK(gpio_set_level(clock, 0));
        ESP_ERROR_CHECK(gpio_set_level(data, (command >> bit) & 1));
        ESP_ERROR_CHECK(gpio_set_level(clock, 1));
    }
    ESP_ERROR_CHECK(gpio_set_direction(data, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_pullup_en(data)); // A CO5300 returns 0xff.
    uint8_t id = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        ESP_ERROR_CHECK(gpio_set_level(clock, 0));
        esp_rom_delay_us(1);
        id = (id << 1) | gpio_get_level(data);
        ESP_ERROR_CHECK(gpio_set_level(clock, 1));
        esp_rom_delay_us(1);
    }
    ESP_ERROR_CHECK(gpio_set_level(cs, 1));
    return id;
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    uint8_t id = panel_id();
    static lgfx::Bus_SPI bus;
    example_qspi(bus, 10, 11, 12, 13, 14);
    if (id == 0x86) {
        static lgfx::Panel_SH8601Z panel;
        example_draw_display(panel, bus, "EspHub", 9, 21, -1, 466, 466, 0, 0, false);
    } else if (id == 0xff) {
        static lgfx::Panel_CO5300 panel;
        example_draw_display(panel, bus, "EspHub", 9, 21, -1, 466, 466, 6, 0, false);
    } else {
        ESP_LOGE("display", "Unknown AMOLED ID 0x%02x", id);
    }
}
