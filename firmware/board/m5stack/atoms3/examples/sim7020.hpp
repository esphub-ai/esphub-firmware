// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once

#include "driver/uart.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace sim7020 {
constexpr auto modem = UART_NUM_2;
constexpr unsigned chunkSize = 256;

inline void initUart(uart_port_t port, int tx, int rx, int buffer)
{
    uart_config_t config{};
    config.baud_rate = 115200;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_APB;
    ESP_ERROR_CHECK(uart_param_config(port, &config));
    ESP_ERROR_CHECK(uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(port, buffer, 256, 0, nullptr, 0));
    std::printf("UART%d 115200 8N1 TX=%d RX=%d\n", int(port), tx, rx);
}

enum class Reply { ok, rejected, timeout };

inline bool write(std::string_view data)
{
    return uart_write_bytes(modem, data.data(), data.size()) == int(data.size());
}

inline bool readLine(std::string& line, int64_t deadline, bool prompt = false)
{
    line.clear();
    while (esp_timer_get_time() < deadline) {
        uint8_t byte;
        if (uart_read_bytes(modem, &byte, 1, pdMS_TO_TICKS(20)) != 1) continue;
        if (prompt && byte == '>' && line.empty()) {
            line = ">";
            return true;
        }
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (!line.empty()) return true;
            continue;
        }
        if (line.empty() && byte == ' ') continue;
        if (line.size() >= 640) return false;
        line.push_back(char(byte));
    }
    return false;
}

bool nonzeroIpv4(const char* text, bool allowSubnet = false)
{
    unsigned a, b, c, d;
    int end = 0;
    return std::sscanf(text, "%u.%u.%u.%u%n", &a, &b, &c, &d, &end) == 4 &&
           a <= 255 && b <= 255 && c <= 255 && d <= 255 && (a || b || c || d) &&
           (text[end] == '\0' || (allowSubnet && (text[end] == '.' || text[end] == '"' || text[end] == ',')));
}

inline Reply await(std::string_view expected, unsigned timeoutMs, std::string* data = nullptr, bool* closed = nullptr)
{
    const auto deadline = esp_timer_get_time() + int64_t(timeoutMs) * 1000;
    std::string line;
    bool haveData = false;
    bool haveOk = false;
    while (readLine(line, deadline, expected == ">")) {
        if (line == "ERROR" || line == "FAIL" || line.ends_with("CONNECT FAIL") ||
            line.ends_with("SEND FAIL") || line.starts_with("+CME ERROR") ||
            line.starts_with("+CMS ERROR")) return Reply::rejected;
        if (closed && (line == "0,CLOSED" || line == "CLOSED")) *closed = true;
        if (line == "OK") haveOk = true;
        if (expected == "IPv4" && nonzeroIpv4(line.c_str())) return Reply::ok;
        if (!expected.empty() && (expected.starts_with('+') ? line.starts_with(expected) : line.ends_with(expected))) {
            if (!data) return Reply::ok;
            *data = line;
            haveData = true;
        }
        if (haveOk && (expected.empty() || haveData)) return Reply::ok;
    }
    return Reply::timeout;
}

inline Reply command(std::string_view cmd, std::string_view expected = {}, unsigned timeoutMs = 3000,
              std::string* data = nullptr, bool* closed = nullptr)
{
    std::string framed(cmd);
    framed += "\r\n";
    const auto result = write(framed) ? await(expected, timeoutMs, data, closed) : Reply::rejected;
    if (result != Reply::ok) {
        std::printf("%.*s: %s\n", int(cmd.size()), cmd.data(),
                    result == Reply::timeout ? "timeout" : "modem error");
    }
    return result;
}

inline bool ready()
{
    for (unsigned i = 0; i < 3; ++i) {
        if (command("AT", {}, 2500) == Reply::ok) return true;
    }
    return false;
}

inline bool hasAddress(const std::string& row)
{
    // +CGCONTRDP returns CID, bearer, APN, local address/subnet, gateway, DNS.
    const char* field = row.c_str();
    for (unsigned i = 0; i < 3; ++i) {
        field = std::strchr(field, ',');
        if (!field) return false;
        ++field;
    }
    while (*field == ' ' || *field == '"') ++field;
    return nonzeroIpv4(field, true);
}

inline bool attach(void (*show)(const char*))
{
    if (!ready()) return false;
    command("AT+CRESET", {}, 500);
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!ready() || command("AT+CFUN=1") != Reply::ok) return false;
    show("APN cmnbiot");
    if (command("AT+CFUN=0") != Reply::ok ||
        command("AT*MCGDEFCONT=\"IP\",\"cmnbiot\"") != Reply::ok ||
        command("AT+CFUN=1") != Reply::ok) return false;
    command("AT+CGATT?");
    std::string parameters;
    command("AT+CGCONTRDP", "+CGCONTRDP:", 3000, &parameters);
    // The TCP path addresses socket 0 and explicitly reads buffered data.
    if (command("AT+CIPMUX=1") != Reply::ok || command("AT+CIPRXGET=1") != Reply::ok) return false;
    command("AT+CIICR");
    command("AT+CIFSR", "IPv4");

    show("Waiting for PDP");
    const auto deadline = esp_timer_get_time() + 60000000;
    while (esp_timer_get_time() < deadline) {
        std::string row;
        if (command("AT+CGCONTRDP=", "+CGCONTRDP:", 3000, &row) == Reply::ok && hasAddress(row)) {
            show("PDP connected");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

inline int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool receive(unsigned count, std::string& output, bool& closed, size_t limit)
{
    char cmd[40];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPRXGET=3,0,%u", count);
    if (!write(std::string(cmd) + "\r\n")) return false;
    const auto deadline = esp_timer_get_time() + 3000000;
    std::string line;
    while (readLine(line, deadline)) {
        if (line == "0,CLOSED" || line == "CLOSED") closed = true;
        if (line == "ERROR" || line.starts_with("+CME ERROR")) return false;
        unsigned received, remaining;
        if (std::sscanf(line.c_str(), "+CIPRXGET: 3,0,%u,%u", &received, &remaining) != 2) continue;
        if (!received || received > count || output.size() + received > limit ||
            !readLine(line, deadline) || line.size() != 2 * received) return false;
        for (unsigned i = 0; i < received; ++i) {
            int hi = hexDigit(line[2 * i]), lo = hexDigit(line[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            output.push_back(char(hi * 16 + lo));
        }
        return await({}, 3000, nullptr, &closed) == Reply::ok;
    }
    return false;
}
}
