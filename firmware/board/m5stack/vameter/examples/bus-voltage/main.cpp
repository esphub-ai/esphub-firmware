// Adapted from https://github.com/m5stack/VAMeter-Firmware/blob/5e8f847bbc771da2d354be7472c6345acb5655ac/platforms/vameter/main/hal_vameter/components/hal_power_monitor.cpp
// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include <M5Unified.h>
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.setTextSize(2);
    constexpr uint8_t ina226 = 0x40;
    constexpr uint32_t i2c_hz = 100000;
    // Manufacturer settings: average 16 samples, 1.1 ms for bus and shunt, continuous mode.
    constexpr uint8_t config[] = {0x05, 0x27};
    M5.In_I2C.writeRegister(ina226, 0x00, config, sizeof(config), i2c_hz);
    for (;;) {
        uint8_t raw[2];
        M5.Display.setCursor(0, 0);
        if (M5.In_I2C.readRegister(ina226, 0x02, raw, sizeof(raw), i2c_hz)) {
            const float volts = ((raw[0] << 8) | raw[1]) * 0.00125f;
            M5.Display.printf("Bus: %.3f V    ", volts);
        } else {
            M5.Display.print("INA226 offline  ");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
