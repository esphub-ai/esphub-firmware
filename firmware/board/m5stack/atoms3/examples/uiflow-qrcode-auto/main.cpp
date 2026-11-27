// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr auto port = UART_NUM_2;
constexpr auto tx = GPIO_NUM_6;
constexpr auto rx = GPIO_NUM_5;
constexpr auto trigger = GPIO_NUM_7;
constexpr auto done = GPIO_NUM_8;
constexpr uint8_t automaticMode[] = {0x07, 0xc6, 0x04, 0x08, 0x00, 0x8a, 0x04, 0xfe, 0x99};
constexpr uint8_t expectedAck[] = {0x04, 0xd0, 0x00, 0x00, 0xff, 0x2c};
constexpr size_t maxFrame = 1024;

void drawRow(int y, const char* text, uint16_t color)
{
    auto& lcd = M5.Display;
    lcd.fillRect(5, y, lcd.width() - 5, lcd.fontHeight(), TFT_BLACK);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.drawString(text, 5, y);
}

bool initScanner()
{
    const uart_config_t config{
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    if (gpio_set_level(trigger, 1) != ESP_OK
        || gpio_set_direction(trigger, GPIO_MODE_OUTPUT) != ESP_OK
        || gpio_set_direction(done, GPIO_MODE_INPUT) != ESP_OK
        || gpio_set_pull_mode(done, GPIO_PULLDOWN_ONLY) != ESP_OK
        || uart_param_config(port, &config) != ESP_OK
        || uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK
        || uart_driver_install(port, 2048, 0, 0, nullptr, 0) != ESP_OK
        || uart_set_rx_full_threshold(port, 1) != ESP_OK) {
        std::puts("QRCode UART/GPIO setup failed");
        return false;
    }
    std::puts("QRCode UART2 9600 8N1 TX=6 RX=5 TRIG=7 idle high DONE=8");
    const uint8_t wake = 0;
    if (uart_write_bytes(port, &wake, sizeof(wake)) != sizeof(wake)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int attempt = 1; attempt <= 3; ++attempt) {
        uart_flush_input(port);
        if (uart_write_bytes(port, automaticMode, sizeof(automaticMode)) != sizeof(automaticMode)) {
            return false;
        }
        uint8_t ack[sizeof(expectedAck)];
        size_t count = 0;
        const int64_t deadline = esp_timer_get_time() + 300000;
        while (count < sizeof(ack) && esp_timer_get_time() < deadline) {
            const int received = uart_read_bytes(port, ack + count, sizeof(ack) - count, 1);
            if (received > 0) {
                count += received;
            }
        }
        if (count == sizeof(ack) && std::memcmp(ack, expectedAck, sizeof(ack)) == 0) {
            std::printf("QRCode AUTO ACK attempt=%d\n", attempt);
            return true;
        }
        std::printf("QRCode AUTO %s attempt=%d\n", count == sizeof(ack) ? "bad ACK" : "timeout", attempt);
        if (attempt < 3) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    return false;
}

// Present arbitrary scanner bytes without injecting control sequences into the serial console.
std::string displayText(const uint8_t* frame, size_t size, bool& binary)
{
    std::string text;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = frame[i];
        if (byte < ' ' || byte > '~') {
            binary = true;
        }
        if (byte < ' ' || byte > '~' || byte == '\\') {
            char hex[5];
            std::snprintf(hex, sizeof(hex), "\\x%02x", unsigned(byte));
            text += hex;
        } else {
            text += char(byte);
        }
    }
    return text;
}

bool readFrame()
{
    uint8_t frame[maxFrame];
    size_t size = 0;
    bool overflow = false;
    int64_t lastByte = esp_timer_get_time();
    const int64_t deadline = lastByte + 500000;
    while (esp_timer_get_time() < deadline) {
        uint8_t bytes[64];
        const int count = uart_read_bytes(port, bytes, sizeof(bytes), 0);
        const int64_t now = esp_timer_get_time();
        if (count > 0) {
            lastByte = now;
            if (!overflow && size + count <= sizeof(frame)) {
                std::memcpy(frame + size, bytes, count);
                size += count;
            } else {
                overflow = true;
            }
        } else if (now - lastByte >= 3000) {
            if (overflow) {
                std::puts("QRCode frame too long");
            } else if (size) {
                bool binary = false;
                const auto text = displayText(frame, size, binary);
                std::printf("QRCode %s: %s\n", binary ? "bytes" : "data", text.c_str());
                auto& lcd = M5.Display;
                // Keep the original single-line label, shortening only the LCD preview.
                std::string label;
                for (char character : text) {
                    label += character;
                    if (lcd.textWidth(label.c_str()) > lcd.width() - 5) {
                        do {
                            label.pop_back();
                        } while (lcd.textWidth((label + "...").c_str()) > lcd.width() - 5);
                        label += "...";
                        break;
                    }
                }
                drawRow(60, label.c_str(), TFT_WHITE);
            }
            return true;
        }
        vTaskDelay(1);
    }
    uart_flush_input(port);
    std::puts("QRCode frame timeout");
    return false;
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::DejaVu18);
    lcd.setTextDatum(top_left);
    lcd.fillRect(0, 0, lcd.width(), lcd.fontHeight(), TFT_BLUE);
    lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    lcd.drawString("QRCode", 3, 0);
    drawRow(25, "scanning", TFT_GREEN);
    drawRow(60, "data", TFT_WHITE);

    const bool configured = initScanner();
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode scanner unavailable");
    }

    bool waitForDoneLow = false;
    for (;;) {
        M5.update();
        if (configured) {
            if (!gpio_get_level(done)) {
                if (waitForDoneLow) {
                    uart_flush_input(port);
                    waitForDoneLow = false;
                }
            } else if (!waitForDoneLow) {
                waitForDoneLow = !readFrame();
            }
        }
        vTaskDelay(1);
    }
}
