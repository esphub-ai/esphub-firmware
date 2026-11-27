// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once
#include "qrcode_base.hpp"

namespace atoms3_qrcode2 {
using namespace atoms3_qrcode;
constexpr int64_t quietUs = 3000;
constexpr int64_t frameWindowUs = 50000;

inline bool initScanner()
{
    const uart_config_t config{
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    // The M14 base pulls TRIG high and uses the fourth data pin for its LED.
    if (gpio_set_level(trigger, 1) != ESP_OK
        || gpio_set_direction(trigger, GPIO_MODE_OUTPUT) != ESP_OK
        || uart_param_config(port, &config) != ESP_OK
        || uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK
        || uart_driver_install(port, 2048, 0, 0, nullptr, 0) != ESP_OK
        || uart_set_rx_full_threshold(port, 1) != ESP_OK) {
        std::puts("QRCode2 UART/GPIO setup failed");
        return false;
    }
    std::puts("QRCode2 UART2 115200 8N1 TX=6 RX=5 TRIG=7 idle high");
    return true;
}

template <size_t count>
bool writeCommand(const uint8_t (&command)[count])
{
    uart_flush_input(port);
    return uart_write_bytes(port, command, count) == count;
}

template <size_t commandSize, size_t ackSize>
bool exchange(const char* name, const uint8_t (&command)[commandSize],
              const uint8_t (&expected)[ackSize], int timeoutMs)
{
    if (!writeCommand(command)) {
        std::printf("QRCode2 %s write failed\n", name);
        return false;
    }
    uint8_t reply[ackSize];
    size_t size = 0;
    const auto deadline = esp_timer_get_time() + timeoutMs * 1000;
    while (size < sizeof(reply) && esp_timer_get_time() < deadline) {
        const int count = uart_read_bytes(port, reply + size, sizeof(reply) - size, 1);
        if (count > 0) {
            size += count;
        }
    }
    if (size != sizeof(reply)) {
        std::printf("QRCode2 %s timeout (%u/%u bytes)\n", name, unsigned(size), unsigned(ackSize));
        return false;
    }
    if (std::memcmp(reply, expected, ackSize) != 0) {
        std::printf("QRCode2 %s bad ACK\n", name);
        return false;
    }
    std::printf("QRCode2 %s ACK\n", name);
    return true;
}

inline void showResult(const uint8_t* data, size_t size)
{
    bool binary = false;
    auto text = displayText(data, size, binary);
    std::printf("QRCode2 %s: %s\n", binary ? "bytes" : "data", text.c_str());
    auto& lcd = M5.Display;
    std::string preview;
    for (char character : text) {
        preview += character;
        if (lcd.textWidth(preview.c_str()) > lcd.width() - 5) {
            do {
                preview.pop_back();
            } while (lcd.textWidth((preview + "...").c_str()) > lcd.width() - 5);
            preview += "...";
            break;
        }
    }
    drawRow(60, preview.c_str(), TFT_WHITE);
}

class Receiver {
public:
    void reset()
    {
        active = false;
        discard = false;
        size = 0;
    }

    void poll()
    {
        uint8_t bytes[128];
        const int count = uart_read_bytes(port, bytes, sizeof(bytes), 0);
        const auto now = esp_timer_get_time();
        if (count <= 0) {
            if (active && now - lastByte >= quietUs) {
                if (!discard) {
                    if (lastByte - firstByte + quietUs <= frameWindowUs) {
                        showResult(frame, size);
                    } else {
                        std::puts("QRCode2 frame timeout");
                    }
                }
                reset();
            } else if (active && !discard && now - firstByte >= frameWindowUs) {
                std::puts("QRCode2 frame timeout");
                discard = true;
            }
            return;
        }

        if (!active) {
            active = true;
            firstByte = now;
        }
        lastByte = now;
        if (discard) {
            return;
        }
        if (now - firstByte >= frameWindowUs || size + count > sizeof(frame)) {
            std::puts(now - firstByte >= frameWindowUs ? "QRCode2 frame timeout" : "QRCode2 frame too long");
            // Drain this incomplete burst until silence before admitting a new result.
            discard = true;
            return;
        }
        std::memcpy(frame + size, bytes, count);
        size += count;
    }

private:
    uint8_t frame[maxFrame]{};
    size_t size = 0;
    int64_t firstByte = 0;
    int64_t lastByte = 0;
    bool active = false;
    bool discard = false;
};
}  // namespace atoms3_qrcode2
