// SPDX-License-Identifier: MIT
#pragma once

#include "example_i2c.h"
#include "esp_log.h"

static inline unsigned example_bcd(uint8_t byte)
{
    return 10 * (byte >> 4) + (byte & 0x0f);
}

static inline void example_read_clock(gpio_num_t sda, gpio_num_t scl)
{
    i2c_master_bus_handle_t bus = example_i2c_bus(sda, scl);
    i2c_master_dev_handle_t clock = example_i2c_device(bus, 0x51);
    const uint8_t seconds_register = 0x04;
    for (;;) {
        uint8_t time[3];
        esp_err_t status = i2c_master_transmit_receive(clock, &seconds_register, 1, time, sizeof(time), 100);
        if (status == ESP_OK) {
            ESP_LOGI("clock", "%02u:%02u:%02u%s",
                     example_bcd(time[2] & 0x3f), example_bcd(time[1] & 0x7f), example_bcd(time[0] & 0x7f),
                     time[0] & 0x80 ? " (oscillator stopped; time needs setting)" : "");
        } else {
            ESP_LOGW("clock", "PCF85063 read: %s", esp_err_to_name(status));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
