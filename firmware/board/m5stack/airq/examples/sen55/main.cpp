// https://docs.m5stack.com/en/arduino/m5air_quality/sen55
#include "eh_agent.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr auto sensorEnable = GPIO_NUM_10;
constexpr uint8_t address = 0x69;
constexpr uint32_t frequency = 100000;
constexpr uint16_t reset = 0xd304;
constexpr uint16_t startMeasurement = 0x0021;
constexpr uint16_t readMeasuredValues = 0x03c4;
constexpr size_t fieldCount = 8;

constexpr struct Metric {
    const char* name;
    float scale;
    const char* unit;
} metrics[fieldCount] = {
    {"PM1.0", 10, " ug/m3"}, {"PM2.5", 10, " ug/m3"}, {"PM4.0", 10, " ug/m3"}, {"PM10", 10, " ug/m3"},
    {"Hum", 100, "%"}, {"Temp", 200, " C"}, {"VOC", 10, ""}, {"NOx", 10, ""},
};

bool send(uint16_t command)
{
    auto& bus = M5.In_I2C;
    const uint8_t bytes[] = {static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command)};
    if (!bus.start(address, false, frequency)) {
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

bool readSample(uint16_t (&words)[fieldCount])
{
    if (!send(readMeasuredValues)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    auto& bus = M5.In_I2C;
    if (!bus.start(address, true, frequency)) {
        return false;
    }
    uint8_t bytes[fieldCount * 3];
    const bool read = bus.read(bytes, sizeof(bytes), true);
    const bool stopped = bus.stop();
    if (!read || !stopped) {
        return false;
    }
    for (size_t i = 0; i < fieldCount; ++i) {
        const auto* word = bytes + i*3;
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
    display.setCursor(10, 12);
    display.println("SEN55");
    display.setTextSize(1);
    if (status) {
        display.println(status);
    } else {
        for (size_t i = 0; i < fieldCount; ++i) {
            const auto& metric = metrics[i];
            const bool particulate = i < 4;
            const int value = particulate ? words[i] : static_cast<int16_t>(words[i]);
            char line[32];
            if (words[i] == (particulate ? 0xffff : 0x7fff)) {
                std::snprintf(line, sizeof(line), "%s: N/A", metric.name);
            } else {
                std::snprintf(line, sizeof(line), "%s: %.1f%s", metric.name, value / metric.scale, metric.unit);
            }
            display.println(line);
            std::printf("SEN55 %s\n", line);
        }
    }
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Waiting for data");

    // The AirQ sensor rail is enabled when GPIO10 is driven low.
    ESP_ERROR_CHECK(gpio_set_level(sensorEnable, 0));
    ESP_ERROR_CHECK(gpio_set_direction(sensorEnable, GPIO_MODE_OUTPUT));
    vTaskDelay(pdMS_TO_TICKS(100) + 1);
    if (!send(reset)) {
        std::puts("SEN55: reset failed");
    }
    vTaskDelay(pdMS_TO_TICKS(200) + 1);
    if (!send(startMeasurement)) {
        show("Start failed");
        std::puts("SEN55: start failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(50) + 1);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint16_t words[fieldCount];
        if (readSample(words)) {
            show(nullptr, words);
        } else {
            show("Read failed");
            std::puts("SEN55: read failed");
        }
        M5.update();
    }
}
