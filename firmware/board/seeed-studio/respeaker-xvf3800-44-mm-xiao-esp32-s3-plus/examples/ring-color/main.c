// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_i2c.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    i2c_master_bus_handle_t bus = example_i2c_bus(GPIO_NUM_5, GPIO_NUM_6);
    i2c_master_dev_handle_t xmos = example_i2c_device(bus, 0x2c);
    // On the 44 mm board XMOS GPIO33 supplies the LEDs.
    const uint8_t power[] = {20, 1, 2, 33, 1};
    ESP_ERROR_CHECK(i2c_master_transmit(xmos, power, sizeof(power), 100));
    vTaskDelay(pdMS_TO_TICKS(20));
    // XMOS GPO service (20): solid effect, brightness, little-endian BGR color.
    const uint8_t effect[] = {20, 12, 1, 3};
    const uint8_t brightness[] = {20, 13, 1, 64};
    const uint8_t color[] = {20, 16, 4, 0x20, 0x80, 0xe0, 0};
    ESP_ERROR_CHECK(i2c_master_transmit(xmos, effect, sizeof(effect), 100));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(i2c_master_transmit(xmos, brightness, sizeof(brightness), 100));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(i2c_master_transmit(xmos, color, sizeof(color), 100));
}
