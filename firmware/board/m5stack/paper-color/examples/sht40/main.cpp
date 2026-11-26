// https://docs.m5stack.com/en/arduino/papercolor/sht40
#include "eh_agent.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>
#include <cstdio>

namespace {
constexpr uint8_t address = 0x44;
constexpr uint32_t busFrequency = 400000;
constexpr uint8_t measure = 0xfd;
constexpr int64_t updateInterval = 120 * 1000000;

uint8_t crc8(const uint8_t* bytes)
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

bool sample(float& temperature, float& humidity)
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

void showSample(bool valid, float temperature, float humidity)
{
    auto& display = M5.Display;
    const int x = display.width() / 2;
    const int y = display.height() / 2;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString("SHT40 Sensor", x, y - 105);

    char text[40];
    if (valid) {
        std::snprintf(text, sizeof(text), "Temp: %.1f C", temperature);
        display.setTextColor(RED);
        display.drawString(text, x, y - 22);
        std::snprintf(text, sizeof(text), "Humi: %.1f %%", humidity);
        display.setTextColor(BLUE);
        display.drawString(text, x, y + 34);
    } else {
        display.setTextColor(RED);
        display.drawString("SHT40 reading unavailable", x, y);
    }
    display.endWrite();
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setEpdMode(epd_mode_t::epd_fast);

    int64_t nextSample = esp_timer_get_time();
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (now >= nextSample) {
            nextSample = now + updateInterval;
            float temperature = 0;
            float humidity = 0;
            const bool valid = sample(temperature, humidity);
            showSample(valid, temperature, humidity);
            if (valid) {
                std::printf("SHT40: %.1f C, %.1f %% at %lld us\n", temperature, humidity,
                            static_cast<long long>(now));
            } else {
                std::puts("SHT40 reading unavailable");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
