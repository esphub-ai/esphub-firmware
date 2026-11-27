// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2c_master.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
constexpr uint8_t address = 0x56;
constexpr int timeoutMs = 60;
constexpr unsigned pollMs = 200;

struct Sample {
    std::array<int32_t, 6> readings{};
    std::array<bool, 4> active{};
};

esp_err_t readRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t* data, size_t count)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, count, timeoutMs);
}

esp_err_t writeTriggerSources(i2c_master_dev_handle_t device)
{
    const uint8_t wake[] = {0x20, 0x03};
    const uint8_t sleep[] = {0x21, 0x03};
    const auto result = i2c_master_transmit(device, wake, sizeof(wake), timeoutMs);
    return result == ESP_OK ? i2c_master_transmit(device, sleep, sizeof(sleep), timeoutMs) : result;
}

esp_err_t readSample(i2c_master_dev_handle_t device, Sample& sample)
{
    // OUT, BAT, USB. Each pair is voltage followed by current.
    constexpr uint8_t registers[] = {0x68, 0x6c, 0x70, 0x74, 0x60, 0x64};
    for (unsigned i = 0; i < sample.readings.size(); ++i) {
        uint8_t raw[4];
        const auto result = readRegister(device, registers[i], raw, sizeof(raw));
        if (result != ESP_OK) return result;
        const uint32_t bits = uint32_t(raw[0]) | uint32_t(raw[1]) << 8 |
                              uint32_t(raw[2]) << 16 | uint32_t(raw[3]) << 24;
        const int32_t value = static_cast<int32_t>(bits);
        // Voltage is in centivolts, current in hundredths of a milliamp.
        // Match Python's floor division for negative currents.
        sample.readings[i] = i % 2 == 0 ? value : value / 100 - (value % 100 < 0);
    }
    sample.active[0] = sample.readings[4] > 490;
    for (unsigned i = 1; i < sample.active.size(); ++i) {
        constexpr uint8_t statusRegisters[] = {0x10, 0x11, 0x90};
        uint8_t value;
        const auto result = readRegister(device, statusRegisters[i - 1], &value, 1);
        if (result != ESP_OK) return result;
        sample.active[i] = i == 3 ? value != 0 : value == 0;
    }
    return ESP_OK;
}

void showStatus(const char* text, bool error = false)
{
    M5.Display.fillRect(0, 77, 128, 16, TFT_BLACK);
    M5.Display.setTextColor(error ? TFT_RED : TFT_WHITE);
    M5.Display.drawString(text, 3, 78);
    std::puts(text);
}

void showSample(const Sample& sample)
{
    constexpr const char* labels[] = {"OUT", "BAT", "USB"};
    M5.Display.fillRect(0, 0, 128, 76, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    for (unsigned i = 0; i < 3; ++i) {
        char text[24];
        const int x = 3 + i * 42;
        M5.Display.drawString(labels[i], x, 3);
        std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(sample.readings[i * 2]) * 10);
        M5.Display.drawString(text, x, 24);
        std::snprintf(text, sizeof(text), "%ld", long(sample.readings[i * 2 + 1]));
        M5.Display.drawString(text, x, 42);
    }
    M5.Display.drawString("mV / mA", 3, 60);
    for (unsigned i = 0; i < 4; ++i) {
        const int x = 3 + i * 31;
        const uint16_t color = sample.active[i] ? TFT_DARKGREEN : TFT_DARKGREY;
        M5.Display.fillRect(x, 99, 28, 25, color);
        const char label[] = {"UABC"[i], 0};
        M5.Display.setTextColor(TFT_WHITE, color);
        M5.Display.drawString(label, x + 10, 105);
    }
    std::printf("OUT %lld mV %ld mA | BAT %lld mV %ld mA | USB %lld mV %ld mA\n",
                static_cast<long long>(sample.readings[0]) * 10, long(sample.readings[1]),
                static_cast<long long>(sample.readings[2]) * 10, long(sample.readings[3]),
                static_cast<long long>(sample.readings[4]) * 10, long(sample.readings[5]));
}

void showEvents(const Sample& before, const Sample& now)
{
    if (before.active[0] != now.active[0]) std::puts(now.active[0] ? "USB inserted" : "USB removed");
    for (unsigned i = 1; i <= 2; ++i) {
        if (before.active[i] != now.active[i])
            std::printf("Unit Btn%c %s: %d\n", 'A' + i - 1,
                        now.active[i] ? "pressed" : "released", now.active[i]);
    }
    if (before.active[3] != now.active[3]) std::puts(now.active[3] ? "Charging" : "Not charging");
}

esp_err_t sleepCycle(i2c_master_dev_handle_t device)
{
    // TimerPWRUnit.sleep_cycle(0, 0, 30, 0, 0, 10).
    const uint8_t powerOn[] = {0x30, 0, 0, 30};
    const uint8_t powerOff[] = {0x80, 0, 0, 10};
    const uint8_t save[] = {0xf0, 1};
    const uint8_t cycle[] = {0x41, 1};
    auto result = i2c_master_transmit(device, powerOn, sizeof(powerOn), timeoutMs);
    if (result == ESP_OK) result = i2c_master_transmit(device, powerOff, sizeof(powerOff), timeoutMs);
    if (result == ESP_OK) result = i2c_master_transmit(device, save, sizeof(save), timeoutMs);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(100));
    return i2c_master_transmit(device, cycle, sizeof(cycle), timeoutMs);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setFont(&fonts::Font0);
    showStatus("TimerPWR probe");
    std::puts("Port A I2C0 SCL=1 SDA=2 100 kHz; TimerPWR address 0x56");
    vTaskDelay(pdMS_TO_TICKS(400));

    i2c_master_bus_handle_t bus = nullptr;
    auto result = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
    if (result != ESP_OK) {
        i2c_master_bus_config_t config{};
        config.i2c_port = I2C_NUM_0;
        config.sda_io_num = GPIO_NUM_2;
        config.scl_io_num = GPIO_NUM_1;
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        config.flags.enable_internal_pullup = true;
        result = i2c_new_master_bus(&config, &bus);
    }
    if (result != ESP_OK) {
        showStatus("I2C init failed", true);
        std::puts(esp_err_to_name(result));
        return;
    }

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    i2c_master_dev_handle_t unit = nullptr;
    result = i2c_master_bus_add_device(bus, &config, &unit);
    if (result != ESP_OK) {
        showStatus("I2C device error", true);
        std::puts(esp_err_to_name(result));
        return;
    }
    bool ready = false;
    bool initialized = false;
    bool reportedFault = false;
    Sample previous;
    uint32_t nextPoll = 0;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked() && ready) {
            const auto sent = sleepCycle(unit);
            showStatus(sent == ESP_OK ? "Cycle sent 30/10s" : "Sleep cycle failed", sent != ESP_OK);
            if (sent != ESP_OK) std::puts(esp_err_to_name(sent));
        }
        const uint32_t now = M5.millis();
        if (int32_t(now - nextPoll) >= 0) {
            nextPoll = now + pollMs;
            result = initialized ? ESP_OK : i2c_master_probe(bus, address, timeoutMs);
            Sample sample;
            if (result == ESP_OK) result = readSample(unit, sample);
            if (result == ESP_OK && !initialized) result = writeTriggerSources(unit);
            if (result == ESP_OK) {
                if (!ready) showStatus(initialized ? "TimerPWR recovered" : "BtnA: sleep cycle");
                if (ready) showEvents(previous, sample);
                if (!ready || previous.active != sample.active || previous.readings != sample.readings)
                    showSample(sample);
                previous = sample;
                ready = initialized = true;
                reportedFault = false;
            } else {
                if (!reportedFault) {
                    M5.Display.fillScreen(TFT_BLACK);
                    showStatus(initialized ? "TimerPWR I2C fault" : "TimerPWR not ready", true);
                    std::puts(esp_err_to_name(result));
                    reportedFault = true;
                }
                ready = false;
                nextPoll = now + 1000;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
