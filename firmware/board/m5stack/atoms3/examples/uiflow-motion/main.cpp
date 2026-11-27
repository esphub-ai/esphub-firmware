// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr uint8_t motorAddress = 0x38;
constexpr uint8_t meterAddress = 0x40;
constexpr uint8_t motorRegister = 0x20;
constexpr int timeoutMs = 100;
constexpr int64_t retryUs = 1000000;
constexpr int64_t sampleUs = 250000;
constexpr double currentLsb = 3.6 / 32768;
constexpr uint16_t calibration = static_cast<uint16_t>(0.00512 / (currentLsb * 0.02));
constexpr uint16_t meterConfig = (2 << 9) | (4 << 6) | (4 << 3) | 7;

void row(int y, const char* label)
{
    auto& display = M5.Display;
    display.fillRect(5, y, display.width() - 5, display.fontHeight(), TFT_BLACK);
    display.drawString(label, 5, y);
}

bool addDevice(i2c_master_bus_handle_t bus, uint8_t address, i2c_master_dev_handle_t& device)
{
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, &device) == ESP_OK;
}

bool readWord(i2c_master_dev_handle_t device, uint8_t reg, uint16_t& value)
{
    uint8_t bytes[2];
    if (i2c_master_transmit_receive(device, &reg, 1, bytes, sizeof(bytes), timeoutMs) != ESP_OK) {
        return false;
    }
    value = (uint16_t(bytes[0]) << 8) | bytes[1];
    return true;
}

bool writeWord(i2c_master_dev_handle_t device, uint8_t reg, uint16_t value)
{
    const uint8_t bytes[] = {reg, uint8_t(value >> 8), uint8_t(value)};
    return i2c_master_transmit(device, bytes, sizeof(bytes), timeoutMs) == ESP_OK;
}

int signedWord(uint16_t value)
{
    return value < 0x8000 ? value : int(value) - 0x10000;
}

struct Telemetry {
    double voltage, current, power;

    bool read(i2c_master_dev_handle_t device)
    {
        uint16_t bus, amps, watts;
        if (!readWord(device, 0x02, bus) || !readWord(device, 0x04, amps)
            || !readWord(device, 0x03, watts)) {
            return false;
        }
        voltage = bus * 0.00125;
        current = signedWord(amps) * currentLsb;
        // Match UiFlow2's signed power decoding as well as its signed current decoding.
        power = signedWord(watts) * (25 * currentLsb);
        return true;
    }

    void show() const
    {
        char text[24];
        std::snprintf(text, sizeof(text), "vol: %.2f", voltage);
        row(55, text);
        std::snprintf(text, sizeof(text), "cur: %.3f", current);
        row(75, text);
        std::snprintf(text, sizeof(text), "pow: %.2f", power);
        row(95, text);
        std::printf("INA226 vol=%.2f cur=%.3f pow=%.2f\n", voltage, current, power);
    }
};

void showUnavailable(const char* reason)
{
    row(55, reason);
    row(75, "cur: --");
    row(95, "pow: --");
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setFont(&fonts::DejaVu18);
    display.setTextDatum(top_left);
    display.fillRect(0, 0, display.width(), display.fontHeight(), TFT_BLUE);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.drawString("Motor Ctrl", 3, 0);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    row(27, "speed: 0");
    row(55, "vol: --");
    row(75, "cur: --");
    row(95, "pow: --");
    std::puts("Motor Ctrl: speed: 0");

    // Use the bus already created by M5Unified for the internal IMU.
    const auto& pins = M5.In_I2C;
    std::printf("Motion I2C%d SDA=%d SCL=%d 100 kHz\n",
                int(pins.getPort()), int(pins.getSDA()), int(pins.getSCL()));
    i2c_master_bus_handle_t bus = nullptr;
    i2c_master_dev_handle_t motor = nullptr, meter = nullptr;
    bool motorReady = false, meterReady = false, stopRequired = false;
    bool motorMissingShown = false;
    int speed = 0;
    int64_t nextMotorProbe = 0, nextSample = esp_timer_get_time() + sampleUs;
    const bool hasBus = i2c_master_get_bus_handle(pins.getPort(), &bus) == ESP_OK;
    if (hasBus) {
        addDevice(bus, motorAddress, motor);
        addDevice(bus, meterAddress, meter);
    }
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (!motorReady && now >= nextMotorProbe) {
            nextMotorProbe = now + retryUs;
            motorReady = motor && i2c_master_probe(bus, motorAddress, timeoutMs) == ESP_OK;
            if (!motorReady && !motorMissingShown) {
                row(27, "Motor absent");
                std::puts("Motion controller 0x38 unavailable");
                motorMissingShown = true;
            } else if (motorReady && motorMissingShown) {
                row(27, stopRequired ? "Click to stop" : "speed: 0");
                motorMissingShown = false;
            }
        }
        if (M5.BtnA.wasClicked() && motorReady) {
            const int requested = stopRequired || speed == 120 ? 0 : speed + 20;
            const uint8_t command[] = {motorRegister, uint8_t(static_cast<int8_t>(requested))};
            if (i2c_master_transmit(motor, command, sizeof(command), timeoutMs) == ESP_OK) {
                speed = requested;
                stopRequired = false;
                char text[24];
                std::snprintf(text, sizeof(text), "speed: %d", speed);
                row(27, text);
                std::printf("Motor 1 %s\n", text);
            } else {
                // A failed write leaves the physical motor state unknown.
                motorReady = false;
                stopRequired = true;
                nextMotorProbe = now + retryUs;
                row(27, "Motor error");
                std::puts("Motion controller 0x38 speed write failed");
            }
        }
        if (now >= nextSample) {
            Telemetry values{};
            if (!meterReady && meter) {
                uint16_t id;
                meterReady = readWord(meter, 0xfe, id) && id == 0x5449
                    && writeWord(meter, 0x00, meterConfig)
                    && writeWord(meter, 0x05, calibration);
                if (meterReady) {
                    std::printf("INA226 config=0x%04x calibration=0x%04x\n", meterConfig, calibration);
                    nextSample = now + sampleUs; // Wait for a completed 16-sample conversion.
                    continue;
                }
                showUnavailable("No INA226");
                std::puts("INA226 0x40 unavailable");
            } else if (meterReady && values.read(meter)) {
                values.show();
                nextSample = now + sampleUs;
                continue;
            } else {
                showUnavailable(meter ? "INA226 error" : "No INA226");
                std::puts(meter ? "INA226 0x40 read failed" : "INA226 0x40 unavailable");
                meterReady = false;
            }
            nextSample = now + retryUs;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
