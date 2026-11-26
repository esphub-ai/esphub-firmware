// Adapted from the MIT-licensed M5AtomS3 I2C Tester.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Advanced/I2C_Tester/I2C_Tester.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
constexpr uint8_t firstAddress = 0x08;
constexpr uint8_t lastAddress = 0x77;
constexpr uint32_t frequencyHz = 100000;

void showResults(const uint8_t* addresses, unsigned count)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(BLACK);
    display.setCursor(4, 2);
    display.printf("I2C HEX: %u found", count);
    for (unsigned i = 0; i < count; ++i) {
        // 9 columns fit the full unicast range on the 128x128 LCD.
        display.setCursor(4 + 14 * (i % 9), 16 + 8 * (i / 9));
        display.printf("%02X", unsigned(addresses[i]));
    }
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(BLACK);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(4, 2);
    display.print("I2C Tester");

    auto& bus = M5.Ex_I2C;
    if (!bus.begin()) {
        display.setCursor(4, 16);
        display.print("Port A unavailable");
        std::puts("I2C Tester: external bus initialization failed");
        return;
    }
    std::printf("I2C Tester: external SDA=%d SCL=%d at %lu Hz\n",
                bus.getSDA(), bus.getSCL(), static_cast<unsigned long>(frequencyHz));
    vTaskDelay(pdMS_TO_TICKS(3000));

    std::array<uint8_t, lastAddress - firstAddress + 1> addresses{};
    for (;;) {
        unsigned count = 0;
        for (uint8_t address = firstAddress; address <= lastAddress; ++address) {
            if (bus.scanID(address, frequencyHz)) {
                addresses[count++] = address;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        std::printf("I2C scan: %u found", count);
        for (unsigned i = 0; i < count; ++i) {
            std::printf(" %02X", unsigned(addresses[i]));
        }
        std::putchar('\n');
        showResults(addresses.data(), count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
