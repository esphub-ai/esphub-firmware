// Adapted for EspHub from https://github.com/m5stack/StackChan-BSP/blob/e2f3b02dbe7f28e26a02adc72bef3aa2c545e4a0/examples/RGB_LED/RGB_LED.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>

static constexpr uint8_t expander = 0x6f;
static constexpr uint32_t i2c_hz = 100000;
static constexpr uint8_t led_config = 0x24;
static constexpr uint8_t led_ram = 0x30;

struct Color_t {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

static constexpr Color_t colors[] = {
    {0, 0, 0}, {168, 0, 0}, {0, 168, 0}, {0, 0, 168}, {168, 168, 0}, {168, 0, 168}, {0, 168, 168}, {168, 168, 168},
};

void setup()
{
    M5.begin();
    // Allow the body controller to start, then configure its RGB output (pin 13).
    vTaskDelay(pdMS_TO_TICKS(1200));
    M5.In_I2C.bitOn(expander, 0x04, 1 << 5, i2c_hz);
    M5.In_I2C.bitOn(expander, 0x0a, 1 << 5, i2c_hz);
    M5.In_I2C.bitOff(expander, 0x14, 1 << 5, i2c_hz);
    M5.In_I2C.writeRegister8(expander, led_config, 12, i2c_hz);
}

void loop()
{
    /* There are 12 RGB LEDs, index 0-5 are on the left, 6-11 are on the right */
    for (const auto color : colors) {
        uint16_t rgb = ((color.r & 0xf8) << 8) | ((color.g & 0xfc) << 3) | (color.b >> 3);
        const uint8_t pixels[] = {static_cast<uint8_t>(rgb), static_cast<uint8_t>(rgb >> 8)};
        for (int led_index = 0; led_index < 12; led_index++) {
            M5.In_I2C.writeRegister(expander, led_ram + 2 * led_index, pixels, sizeof(pixels), i2c_hz);
            M5.In_I2C.bitOn(expander, led_config, 1 << 6, i2c_hz);
            vTaskDelay(pdMS_TO_TICKS(1000 / 24));
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
    for (;;) {
        loop();
        vTaskDelay(1);
    }
}
