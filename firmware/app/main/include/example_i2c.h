// SPDX-License-Identifier: MIT
#pragma once

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline i2c_master_bus_handle_t example_i2c_bus(gpio_num_t sda, gpio_num_t scl)
{
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus));
    return bus;
}

static inline i2c_master_dev_handle_t example_i2c_device(i2c_master_bus_handle_t bus, uint8_t address)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address, .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t device;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &device));
    return device;
}

// Change only the selected bits so other rails and GPIOs keep their current state.
static inline void example_update_bits(i2c_master_dev_handle_t device, uint8_t reg,
                                       uint8_t mask, uint8_t value)
{
    uint8_t original;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(device, &reg, 1, &original, 1, 100));
    uint8_t data[] = {reg, (uint8_t)((original & ~mask) | (value & mask))};
    ESP_ERROR_CHECK(i2c_master_transmit(device, data, sizeof(data), 100));
}
