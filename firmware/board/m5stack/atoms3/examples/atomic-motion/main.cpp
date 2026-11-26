// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicMotion example.
#include "eh_agent.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr uint16_t address = 0x38;
constexpr uint32_t busFrequency = 100000;
constexpr int timeoutMs = 100;
constexpr uint8_t motorRegister = 0x20;
constexpr uint8_t servoRegister = 0x00;

void showMessage(const char* message)
{
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setTextSize(2);
    if (display.textWidth(message) > display.width()) {
        display.setTextSize(1);
    }
    display.drawString(message, display.width() / 2, display.height() / 2);
}

esp_err_t writeAndRead(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value, uint8_t& readback)
{
    const uint8_t command[] = {reg, value};
    esp_err_t result = i2c_master_transmit(device, command, sizeof(command), timeoutMs);
    if (result == ESP_OK) {
        result = i2c_master_transmit(device, &reg, 1, timeoutMs);
    }
    if (result == ESP_OK) {
        result = i2c_master_receive(device, &readback, 1, timeoutMs);
    }
    return result;
}

void runPhase(i2c_master_dev_handle_t device, uint8_t firstRegister, unsigned channels, int value)
{
    const bool motor = firstRegister == motorRegister;
    const char* kind = motor ? "Motor" : "Servo";
    for (unsigned channel = 0; channel < channels; ++channel) {
        uint8_t readback = 0;
        const esp_err_t result = writeAndRead(device, firstRegister + channel,
                                             static_cast<uint8_t>(value), readback);
        if (result == ESP_OK) {
            const int reported = motor && readback >= 128 ? int(readback) - 256 : int(readback);
            std::printf("%s Channel %u: %d\n", kind, channel, reported);
        } else {
            std::printf("%s Channel %u: I2C error (%s)\n", kind, channel, esp_err_to_name(result));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    showMessage("Atomic Init");
    // The base shares the AtomS3 IMU bus on GPIO38/39. M5Unified owns it.
    const auto& pins = M5.In_I2C;
    std::printf("Atomic Motion: I2C%d SDA=%d SCL=%d address=0x38 clock=100000 Hz\n",
                int(pins.getPort()), int(pins.getSDA()), int(pins.getSCL()));
    vTaskDelay(pdMS_TO_TICKS(250));

    i2c_master_bus_handle_t bus = nullptr;
    bool showedFailure = false;
    for (;;) {
        esp_err_t result = bus ? ESP_OK : i2c_master_get_bus_handle(pins.getPort(), &bus);
        if (result == ESP_OK) {
            result = i2c_master_probe(bus, address, timeoutMs);
        }
        if (result == ESP_OK) {
            break;
        }
        if (!showedFailure) {
            showMessage("Init Fail");
            showedFailure = true;
        }
        std::printf("Atomic Motion begin failed (%s)\n", esp_err_to_name(result));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    i2c_device_config_t deviceConfig{};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = address;
    deviceConfig.scl_speed_hz = busFrequency;
    i2c_master_dev_handle_t device = nullptr;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &deviceConfig, &device));

    showMessage("Motion");
    std::puts("Atomic Motion Test");
    for (;;) {
        runPhase(device, motorRegister, 2, 127);
        runPhase(device, motorRegister, 2, -127);
        runPhase(device, motorRegister, 2, 0);
        runPhase(device, servoRegister, 4, 180);
        runPhase(device, servoRegister, 4, 0);
    }
}
