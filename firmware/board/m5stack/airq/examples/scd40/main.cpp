// https://docs.m5stack.com/en/arduino/m5air_quality/scd40
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr uint8_t address = 0x62;
constexpr uint32_t busFrequency = 100000;
constexpr uint16_t stopPeriodic = 0x3f86;
constexpr uint16_t startPeriodic = 0x21b1;
constexpr uint16_t getDataReady = 0xe4b8;
constexpr uint16_t readMeasurement = 0xec05;

bool send(uint16_t command)
{
    auto& bus = M5.In_I2C;
    const uint8_t bytes[] = {static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command)};
    if (!bus.start(address, false, busFrequency)) {
        return false;
    }
    const bool wrote = bus.write(bytes, sizeof(bytes));
    const bool stopped = bus.stop();
    return wrote && stopped;
}

uint8_t crc8(const uint8_t* word)
{
    uint8_t crc = 0xff;
    for (int i = 0; i < 2; ++i) {
        crc ^= word[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc << 1) ^ ((crc & 0x80) ? 0x31 : 0);
        }
    }
    return crc;
}

bool readWords(uint16_t command, uint16_t* words, size_t count)
{
    if (!send(command)) {
        return false;
    }
    // Both read commands need 1 ms before the response is available.
    vTaskDelay(pdMS_TO_TICKS(1) + 1);
    auto& bus = M5.In_I2C;
    if (!bus.start(address, true, busFrequency)) {
        return false;
    }
    uint8_t response[9];
    const bool read = bus.read(response, count * 3, true);
    const bool stopped = bus.stop();
    if (!read || !stopped) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* word = response + i * 3;
        if (crc8(word) != word[2]) {
            return false;
        }
        words[i] = (word[0] << 8) | word[1];
    }
    return true;
}

void show(const char* status, const uint16_t* words = nullptr)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("SCD40");
    if (status) {
        display.println(status);
    } else {
        const float temperature = -45.0f + 175.0f * words[1] / 65535.0f;
        const float humidity = 100.0f * words[2] / 65535.0f;
        display.printf("CO2: %u ppm\n", words[0]);
        display.printf("Temp: %.1f C\n", temperature);
        display.printf("Hum: %.1f%%\n", humidity);
        std::printf("SCD40: CO2=%u ppm Temp=%.1f C Hum=%.1f%%\n", words[0], temperature, humidity);
    }
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Waiting for data");

    if (!send(stopPeriodic)) {
        std::puts("SCD40: stop periodic measurement failed");
    }
    vTaskDelay(pdMS_TO_TICKS(500) + 1);
    if (!send(startPeriodic)) {
        show("Start failed");
        std::puts("SCD40: start periodic measurement failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        M5.update();
        uint16_t ready = 0;
        if (!readWords(getDataReady, &ready, 1)) {
            std::puts("SCD40: data-ready read failed");
            continue;
        }
        if (!(ready & 0x07ff)) {
            continue;
        }
        uint16_t words[3];
        if (!readWords(readMeasurement, words, 3) || words[0] == 0) {
            show("Read failed");
            std::puts("SCD40: measurement read failed");
            continue;
        }
        show(nullptr, words);
    }
}
