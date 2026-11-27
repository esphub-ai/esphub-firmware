// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace atoms3_qrcode {
constexpr auto port = UART_NUM_2;
constexpr auto tx = GPIO_NUM_6;
constexpr auto rx = GPIO_NUM_5;
constexpr auto trigger = GPIO_NUM_7;
constexpr auto done = GPIO_NUM_8;
constexpr uint8_t expectedAck[] = {0x04, 0xd0, 0x00, 0x00, 0xff, 0x2c};
constexpr size_t maxFrame = 1024;

inline void drawRow(int y, const char* text, uint16_t color)
{
    auto& lcd = M5.Display;
    lcd.fillRect(5, y, lcd.width() - 5, lcd.fontHeight(), TFT_BLACK);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.drawString(text, 5, y);
}

inline bool wakeScanner()
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

    return true;
}

enum class Ack { accepted, timeout, mismatch, writeError };

inline const char* ackText(Ack result)
{
    switch (result) {
        case Ack::accepted: return "ACK";
        case Ack::timeout: return "timeout";
        case Ack::mismatch: return "bad ACK";
        case Ack::writeError: return "write error";
    }
    return "error";
}

template <size_t length>
Ack sendCommand(const uint8_t (&command)[length], int timeoutMs)
{
    uart_flush_input(port);
    if (uart_write_bytes(port, command, length) != length) {
        return Ack::writeError;
    }
    uint8_t ack[sizeof(expectedAck)];
    size_t count = 0;
    const int64_t deadline = esp_timer_get_time() + timeoutMs * 1000;
    while (count < sizeof(ack) && esp_timer_get_time() < deadline) {
        const int received = uart_read_bytes(port, ack + count, sizeof(ack) - count, 1);
        if (received > 0) {
            count += received;
        }
    }
    if (count != sizeof(ack)) {
        return Ack::timeout;
    }
    return std::memcmp(ack, expectedAck, sizeof(ack)) == 0 ? Ack::accepted : Ack::mismatch;
}

template <size_t length>
bool setMode(const char* name, const uint8_t (&command)[length])
{
    for (int attempt = 1; attempt <= 3; ++attempt) {
        const auto result = sendCommand(command, 300);
        if (result == Ack::writeError) {
            return false;
        }
        std::printf("QRCode %s %s attempt=%d\n", name, ackText(result), attempt);
        if (result == Ack::accepted) {
            return true;
        }
        if (attempt < 3) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    return false;
}

// Present arbitrary scanner bytes without injecting control sequences into the serial console.
inline std::string displayText(const uint8_t* frame, size_t size, bool& binary)
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

enum class Frame { empty, displayed, timeout };

inline Frame readFrame()
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
                return Frame::displayed;
            }
            return Frame::empty;
        }
        vTaskDelay(1);
    }
    uart_flush_input(port);
    std::puts("QRCode frame timeout");
    return Frame::timeout;
}

inline void drawScreen(const char* status, uint16_t color)
{
    auto& lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::DejaVu18);
    lcd.setTextDatum(top_left);
    lcd.fillRect(0, 0, lcd.width(), lcd.fontHeight(), TFT_BLUE);
    lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    lcd.drawString("QRCode", 3, 0);
    drawRow(25, status, color);
    drawRow(60, "data", TFT_WHITE);
}
}  // namespace atoms3_qrcode
