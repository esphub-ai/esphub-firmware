// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../sim7028_http.hpp"
#include <M5Unified.h>

namespace {
constexpr uint8_t address = 0x6f;
constexpr uint32_t clockHz = 100000;
constexpr uint8_t gpioMode = 0x03;
constexpr uint8_t gpioOutput = 0x05;
constexpr uint8_t gpioDrive = 0x13;
constexpr uint16_t vin = 1U << (2 - 1);
constexpr uint16_t power = 1U << (9 - 1);
constexpr uint16_t reset = 1U << (11 - 1);

bool updateBits(uint8_t reg, uint16_t mask, uint16_t value)
{
    uint8_t bytes[2];
    auto& bus = M5.In_I2C;
    if (!bus.readRegister(address, reg, bytes, sizeof(bytes), clockHz)) return false;
    const uint16_t previous = bytes[0] | (uint16_t(bytes[1]) << 8);
    const uint16_t updated = (previous & ~mask) | value;
    bytes[0] = updated & 0xff;
    bytes[1] = updated >> 8;
    return bus.writeRegister(address, reg, bytes, sizeof(bytes), clockHz);
}

bool startModemPower()
{
    const auto& bus = M5.In_I2C;
    std::printf("M5IOE1 I2C%d SDA=%d SCL=%d 100000 Hz address=0x6f\n",
                int(bus.getPort()), int(bus.getSDA()), int(bus.getSCL()));
    if (bus.getPort() != I2C_NUM_1 || bus.getSDA() != GPIO_NUM_38 || bus.getSCL() != GPIO_NUM_39 ||
        !updateBits(gpioDrive, vin | power | reset, 0) ||
        !updateBits(gpioMode, vin | power | reset, power | reset) ||
        !updateBits(gpioOutput, power, power)) return false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!updateBits(gpioOutput, reset, 0)) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!updateBits(gpioOutput, reset, reset)) return false;
    vTaskDelay(pdMS_TO_TICKS(3000));
    return true;
}

bool waitForPdp()
{
    using namespace sim7020;
    if (!ready() || command("AT+CFUN=1") != Reply::ok) return false;
    std::puts("Waiting for PDP");
    const auto deadline = esp_timer_get_time() + 60000000;
    while (esp_timer_get_time() < deadline) {
        std::string row;
        if (command("AT+CGCONTRDP=0", "+CGCONTRDP:", 3000, &row) == Reply::ok && hasAddress(row)) {
            std::puts("PDP connected");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

void run()
{
    using namespace sim7020;
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (!startModemPower()) {
        std::puts("M5IOE1 initialization failed; modem not contacted");
        return;
    }
    if (!waitForPdp()) {
        std::puts("Modem/PDP failed");
        return;
    }
    std::puts("HTTP POST");
    std::string content;
    int status = 0;
    if (!sim7028::post(content, status)) {
        std::puts("HTTP failed");
        return;
    }
    std::printf("status code: %d\ncontent: ", status);
    std::fwrite(content.data(), 1, content.size(), stdout);
    std::printf("\nHTTP %d\n", status);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    run();
    while (true) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
