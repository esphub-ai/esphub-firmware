// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());

    // Atom-PSRAM Grove port. The internal I2C pins are GPIO25 and GPIO21.
    i2c_master_bus_config_t config{};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = GPIO_NUM_26;
    config.scl_io_num = GPIO_NUM_32;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    const auto result = i2c_new_master_bus(&config, &bus);
    if (result != ESP_OK) {
        std::printf("I2C Tester: Grove bus initialization failed: %s\n", esp_err_to_name(result));
        return;
    }

    std::puts("M5Atom I2C Tester");
    vTaskDelay(pdMS_TO_TICKS(3000));
    for (;;) {
        std::puts("scanning Address [HEX]");
        for (unsigned address = 0x01; address <= 0x7e; ++address) {
            const auto probe = i2c_master_probe(bus, address, 20);
            if (probe == ESP_OK) {
                std::printf("%02X ", address);
            } else if (probe == ESP_ERR_NOT_FOUND) {
                std::putchar('.');
            } else {
                std::putchar('!');
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        std::putchar('\n');
    }
}
