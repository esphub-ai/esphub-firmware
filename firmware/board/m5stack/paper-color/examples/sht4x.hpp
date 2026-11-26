#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>

namespace paper_color::sht4x {
constexpr uint8_t address = 0x44;
constexpr uint32_t busFrequency = 400000;
constexpr uint8_t measure = 0xfd;

inline uint8_t crc8(const uint8_t* bytes)
{
    uint8_t crc = 0xff;
    for (int i = 0; i < 2; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc << 1) ^ ((crc & 0x80) ? 0x31 : 0);
        }
    }
    return crc;
}

inline bool sample(float& temperature, float& humidity)
{
    auto& bus = M5.In_I2C;
    if (!bus.start(address, false, busFrequency)) {
        return false;
    }
    const bool wrote = bus.write(measure);
    const bool writeStopped = bus.stop();
    if (!wrote || !writeStopped) {
        return false;
    }

    // High-precision measurement takes up to 8.3 ms. Round up to a full tick.
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    if (!bus.start(address, true, busFrequency)) {
        return false;
    }
    uint8_t result[6];
    const bool read = bus.read(result, sizeof(result), true);
    const bool readStopped = bus.stop();
    if (!read || !readStopped || crc8(result) != result[2] || crc8(result + 3) != result[5]) {
        return false;
    }

    const int rawTemp = (result[0] << 8) | result[1];
    const int rawHumidity = (result[3] << 8) | result[4];
    temperature = -45.0f + 175.0f * rawTemp / 65535.0f;
    humidity = std::clamp(-6.0f + 125.0f * rawHumidity / 65535.0f, 0.0f, 100.0f);
    return true;
}
}
