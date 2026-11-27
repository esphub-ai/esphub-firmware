// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once

#include "sim7020.hpp"
#include <M5Unified.h>
#include <algorithm>

namespace nbiot_mqtt {
constexpr std::string_view topic = "testtopic/a";
constexpr size_t packetLimit = 4096;

inline void label(const std::string& text, int x, int y)
{
    auto& screen = M5.Display;
    screen.setFont(&fonts::DejaVu18);
    screen.setTextColor(TFT_WHITE, 0x222222U);
    screen.fillRect(x, y, screen.width() - x, screen.fontHeight(), TFT_BLACK);
    screen.fillRect(x, y, screen.textWidth(text.c_str()), screen.fontHeight(), 0x222222U);
    screen.drawString(text.c_str(), x, y);
}

// UiFlow2 label0 uses Python str(bytes). Escape controls and choose the same quotes.
inline std::string bytesText(std::string_view data)
{
    const char quote = data.find('\'') != data.npos && data.find('"') == data.npos ? '"' : '\'';
    std::string result = "b";
    result += quote;
    for (unsigned char c : data) {
        if (c == '\\' || c == quote) {
            result += '\\';
            result += char(c);
        } else if (c == '\t') result += "\\t";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c < 32 || c >= 127) {
            constexpr char hex[] = "0123456789abcdef";
            result += "\\x";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else result += char(c);
    }
    result += quote;
    return result;
}

enum class Packet { pending, complete, invalid };
inline Packet frame(std::string_view data, size_t& start, size_t& remaining)
{
    remaining = 0;
    for (size_t i = 1; i <= 4 && i < data.size(); ++i) {
        const auto digit = static_cast<unsigned char>(data[i]);
        remaining += (digit & 127U) << (7 * (i - 1));
        if (remaining > packetLimit) return Packet::invalid;
        if (!(digit & 128U)) {
            start = i + 1;
            return data.size() >= start + remaining ? Packet::complete : Packet::pending;
        }
    }
    return data.size() >= 5 ? Packet::invalid : Packet::pending;
}

inline bool incoming(unsigned char type, std::string_view data, int x, int y)
{
    if ((type & 0xf6) != 0x30 || data.size() < 2) return false; // Subscribe QoS 0 only.
    const size_t length = (static_cast<unsigned char>(data[0]) << 8) | static_cast<unsigned char>(data[1]);
    if (!length || length > data.size() - 2) return false;
    if (data.substr(2, length) == topic) {
        const auto text = bytesText(data.substr(2 + length));
        std::printf("MQTT %.*s: %s\n", int(topic.size()), topic.data(), text.c_str());
        label(text, x, y);
    }
    return true;
}

inline void session(bool (*send)(std::string_view),
                    bool (*pendingBytes)(unsigned&, bool&),
                    bool (*readBytes)(unsigned, std::string&, bool&, size_t),
                    int labelX, int labelY)
{
    // MQTT 3.1.1, persistent session, zero keepalive, empty username and password.
    constexpr char connect[] = "\x10\x1e\0\x04" "MQTT\x04\xc0\0\0\0\x0e" "uiflow2-client\0\0\0\0";
    constexpr char subscribe[] = "\x82\x10\0\x01\0\x0b" "testtopic/a\0";
    if (!send({connect, sizeof(connect) - 1})) return;
    std::string buffer;
    enum { connack, suback, subscribed } phase = connack;
    auto deadline = esp_timer_get_time() + 10000000;
    bool closed = false;
    while (phase == subscribed || esp_timer_get_time() < deadline) {
        M5.update();
        unsigned available;
        if (!pendingBytes(available, closed)) return;
        if (available && !readBytes(std::min(available, sim7020::chunkSize), buffer, closed,
                                    packetLimit + sim7020::chunkSize)) return;
        while (!buffer.empty()) {
            size_t start, length;
            const auto result = frame(buffer, start, length);
            if (result == Packet::invalid) return;
            if (result == Packet::pending) break;
            const auto type = static_cast<unsigned char>(buffer[0]);
            const std::string_view data(buffer.data() + start, length);
            if (phase == connack) {
                if (type != 0x20 || length != 2 || (data[0] != 0 && data[0] != 1) || data[1] != 0) return;
                std::puts("MQTT CONNACK");
                if (!send({subscribe, sizeof(subscribe) - 1})) return;
                phase = suback;
                deadline = esp_timer_get_time() + 10000000;
            } else if (phase == suback && type == 0x90) {
                if (data != std::string_view("\0\x01\0", 3)) return;
                std::puts("MQTT SUBACK testtopic/a QoS 0");
                phase = subscribed;
            } else if (!incoming(type, data, labelX, labelY)) return;
            buffer.erase(0, start + length);
        }
        if (closed && !available) break;
        if (!available) vTaskDelay(pdMS_TO_TICKS(200));
    }
    return;
}
}
