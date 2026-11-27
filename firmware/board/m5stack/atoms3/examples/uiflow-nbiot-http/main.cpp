// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <strings.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {
constexpr auto modem = UART_NUM_2;
constexpr unsigned chunkSize = 256;
constexpr size_t responseLimit = 8192;
constexpr char body[] = R"({"message":"Hello from M5Stack!","status":"active"})";

void show(const char* status)
{
    auto& screen = M5.Display;
    screen.fillScreen(TFT_BLACK);
    screen.setTextColor(TFT_GREEN);
    screen.setTextSize(1);
    screen.drawString("NB-IoT HTTP", 4, 8);
    screen.drawString(status, 4, 32);
    std::puts(status);
}

void initUart(uart_port_t port, int tx, int rx, int buffer)
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

bool write(std::string_view data)
{
    return uart_write_bytes(modem, data.data(), data.size()) == int(data.size());
}

bool readLine(std::string& line, int64_t deadline, bool prompt = false)
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

Reply await(std::string_view expected, unsigned timeoutMs, std::string* data = nullptr, bool* closed = nullptr)
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

Reply command(std::string_view cmd, std::string_view expected = {}, unsigned timeoutMs = 3000,
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

bool ready()
{
    for (unsigned i = 0; i < 3; ++i) {
        if (command("AT", {}, 2500) == Reply::ok) return true;
    }
    return false;
}

bool hasAddress(const std::string& row)
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

bool attach()
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

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool receive(unsigned count, std::string& http, bool& closed)
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
        if (!received || received > count || http.size() + received > responseLimit ||
            !readLine(line, deadline) || line.size() != 2 * received) return false;
        for (unsigned i = 0; i < received; ++i) {
            int hi = hexDigit(line[2 * i]), lo = hexDigit(line[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            http.push_back(char(hi * 16 + lo));
        }
        return await({}, 3000, nullptr, &closed) == Reply::ok;
    }
    return false;
}

bool readHttp(std::string& http, int& status)
{
    constexpr auto noLength = std::string::npos;
    size_t length = noLength;
    size_t payload = noLength;
    bool closed = false;
    const auto deadline = esp_timer_get_time() + 15000000;
    while (esp_timer_get_time() < deadline) {
        std::string line;
        unsigned available;
        if (command("AT+CIPRXGET=4,0", "+CIPRXGET: 4,0,", 3000, &line, &closed) != Reply::ok ||
            std::sscanf(line.c_str(), "+CIPRXGET: 4,0,%u", &available) != 1) return false;
        if (!available) {
            if (closed) break;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!receive(std::min(available, chunkSize), http, closed)) return false;
        if (payload == noLength) {
            auto boundary = http.find("\r\n\r\n");
            if (boundary == noLength) continue;
            if (std::sscanf(http.c_str(), "HTTP/%*u.%*u %d", &status) != 1 ||
                status < 100 || status > 599) return false;
            payload = boundary + 4;
            auto pos = http.find("\r\n") + 2;
            while (pos < boundary) {
                if (!strncasecmp(http.c_str() + pos, "Transfer-Encoding:", 18)) return false;
                if (!strncasecmp(http.c_str() + pos, "Content-Length:", 15)) {
                    const char* value = http.c_str() + pos + 15;
                    char* end;
                    const auto parsed = std::strtoul(value, &end, 10);
                    if (end == value || *end != '\r' || parsed > responseLimit - payload) return false;
                    length = parsed;
                }
                pos = http.find("\r\n", pos) + 2;
            }
        }
        if (length != noLength && http.size() - payload >= length) break;
    }
    if (payload == noLength || (length == noLength ? !closed : http.size() - payload < length)) return false;
    http.erase(0, payload);
    if (length != noLength) http.resize(length);
    return true;
}

bool post(std::string& content, int& status)
{
    std::string dns;
    if (command("AT+CDNSGIP=httpbin.org", "+CDNSGIP: 1,", 10000, &dns) != Reply::ok) return false;
    char ip[16];
    if (std::sscanf(dns.c_str(), "+CDNSGIP: 1,\"httpbin.org\",\"%15[0-9.]\"", ip) != 1 ||
        !nonzeroIpv4(ip)) return false;

    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSTART=0,\"TCP\",\"%s\",\"80\"", ip);
    if (command(cmd, "CONNECT OK", 160000) != Reply::ok) return false;

    std::string request =
        "POST /post HTTP/1.0\r\nHost: httpbin.org\r\nContent-Type: application/json\r\n"
        "Custom-Header: MyHeaderValue\r\nContent-Length: ";
    request += std::to_string(sizeof(body) - 1);
    request += "\r\nConnection: close\r\n\r\n";
    request += body;
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%u", unsigned(request.size()));
    const bool result = command(cmd, ">") == Reply::ok && write(request) &&
                        await("SEND OK", 3000) == Reply::ok && readHttp(content, status);
    command("AT+CIPCLOSE=0", "CLOSE OK");
    return result;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Starting modem");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (!attach()) {
        show("Modem/PDP failed");
        return;
    }
    show("HTTP POST");
    std::string content;
    int status = 0;
    if (!post(content, status)) {
        show("HTTP failed");
        return;
    }
    std::printf("status code: %d\ncontent: ", status);
    std::fwrite(content.data(), 1, content.size(), stdout);
    std::putchar('\n');
    char label[32];
    std::snprintf(label, sizeof(label), "HTTP %d", status);
    show(label);
}
