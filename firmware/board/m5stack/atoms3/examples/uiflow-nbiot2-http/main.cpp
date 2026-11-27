// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../sim7020.hpp"
#include <M5Unified.h>
#include <strings.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {
using namespace sim7020;
constexpr size_t responseLimit = 8192;
constexpr char body[] = R"({"message":"Hello from M5Stack!","status":"active"})";

// SIM7028 connect(apn) does not apply its APN argument. Configure the initial
// EPS context explicitly, using the SIM7028 AT manual's CID 0.
bool attach7028()
{
    if (!ready() || command("AT+CFUN=0") != Reply::ok ||
        command("AT+CGDCONT=0,\"IP\",\"cmnbiot\"") != Reply::ok ||
        command("AT+CFUN=1") != Reply::ok) return false;
    const auto deadline = esp_timer_get_time() + 60000000;
    std::puts("Waiting for PDP");
    while (esp_timer_get_time() < deadline) {
        std::string row;
        int rssi, registration, mode;
        if (command("AT+CPIN?", "+CPIN:", 3000, &row) == Reply::ok && row == "+CPIN: READY" &&
            command("AT+CSQ", "+CSQ:", 3000, &row) == Reply::ok &&
            std::sscanf(row.c_str(), "+CSQ: %d,", &rssi) == 1 && rssi >= 2 && rssi <= 31) {
            command("AT+CREG?");
            if (command("AT+CEREG?", "+CEREG:", 3000, &row) == Reply::ok &&
                std::sscanf(row.c_str(), "+CEREG: %d,%d", &mode, &registration) == 2 &&
                (registration == 1 || registration == 5) &&
                command("AT+CPSI?", "+CPSI:", 3000, &row) == Reply::ok &&
                command("AT+CGCONTRDP=0", "+CGCONTRDP:", 3000, &row) == Reply::ok &&
                hasAddress(row)) {
                std::puts("PDP connected");
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

bool openNetwork()
{
    std::string row;
    if (command("AT+NETOPEN?", "+NETOPEN:", 3000, &row) != Reply::ok) return false;
    if (row == "+NETOPEN: 1") return true;
    return row == "+NETOPEN: 0" &&
           command("AT+NETOPEN", "+NETOPEN:", 30000, &row) == Reply::ok &&
           row == "+NETOPEN: 0";
}

bool readSocketLine(std::string& line, int64_t deadline, bool& closed)
{
    while (readLine(line, deadline)) {
        if (line == "+IPCLOSE: 0,1") {
            closed = true;
            continue;
        }
        if (line.starts_with("+IPCLOSE: 0,") || line.starts_with("+CIPEVENT: NETWORK CLOSED"))
            return false;
        return true;
    }
    return false;
}

bool socketRead(unsigned count, std::string& data, bool& closed)
{
    char cmd[40];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPRXGET=3,0,%u\r\n", count);
    if (!write(cmd)) return false;
    const auto deadline = esp_timer_get_time() + 3000000;
    std::string line;
    unsigned received, remaining;
    while (readSocketLine(line, deadline, closed)) {
        if (line == "ERROR" || line.starts_with("+CME ERROR")) return false;
        if (std::sscanf(line.c_str(), "+CIPRXGET: 3,0,%u,%u", &received, &remaining) != 2) continue;
        if (!received || received > count || data.size() + received > responseLimit ||
            !readSocketLine(line, deadline, closed) || line.size() != 2 * received) return false;
        for (unsigned i = 0; i < received; ++i) {
            const int hi = hexDigit(line[2 * i]);
            const int lo = hexDigit(line[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            data.push_back(char(hi * 16 + lo));
        }
        while (readSocketLine(line, deadline, closed)) {
            if (line == "OK") return true;
            if (line == "ERROR" || line.starts_with("+CME ERROR")) return false;
        }
        return false;
    }
    return false;
}

bool pendingBytes(unsigned& amount, bool& closed)
{
    if (!write("AT+CIPRXGET=4,0\r\n")) return false;
    const auto deadline = esp_timer_get_time() + 3000000;
    std::string line;
    bool count = false;
    amount = 0;
    while (readSocketLine(line, deadline, closed)) {
        if (line == "OK") return count;
        if (line == "ERROR" || line.starts_with("+CME ERROR")) return closed && !count;
        if (std::sscanf(line.c_str(), "+CIPRXGET: 4,0,%u", &amount) == 1) count = true;
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
        unsigned available;
        if (!pendingBytes(available, closed)) return false;
        if (!available) {
            if (closed) break;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!socketRead(std::min(available, chunkSize), http, closed)) return false;
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

    if (!openNetwork() || command("AT+CIPRXGET=1") != Reply::ok) return false;
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPOPEN=0,\"TCP\",\"%s\",\"80\"", ip);
    std::string reply;
    if (command(cmd, "+CIPOPEN:", 160000, &reply) != Reply::ok || reply != "+CIPOPEN: 0,0")
        return false;

    std::string request =
        "POST /post HTTP/1.0\r\nHost: httpbin.org\r\nContent-Type: application/json\r\n"
        "Custom-Header: MyHeaderValue\r\nContent-Length: ";
    request += std::to_string(sizeof(body) - 1);
    request += "\r\nConnection: close\r\n\r\n";
    request += body;
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%u", unsigned(request.size()));
    char sent[40];
    std::snprintf(sent, sizeof(sent), "+CIPSEND: 0,%u,%u", unsigned(request.size()), unsigned(request.size()));
    const bool result = command(cmd, ">") == Reply::ok && write(request) &&
                        await("+CIPSEND:", 3000, &reply) == Reply::ok && reply == sent &&
                        readHttp(content, status);
    command("AT+CIPCLOSE=0", "+CIPCLOSE:", 10000, &reply);
    return result;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& screen = M5.Display;
    screen.fillScreen(TFT_BLACK);
    screen.setFont(&fonts::DejaVu18);
    screen.setTextColor(TFT_WHITE, 0x222222U);
    screen.fillRect(8, 7, screen.textWidth("label0"), screen.fontHeight(), 0x222222U);
    screen.drawString("label0", 8, 7);
    std::puts("label0");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (!attach7028()) {
        std::puts("Modem/PDP failed");
        return;
    }
    std::puts("HTTP POST");
    std::string content;
    int status = 0;
    if (!post(content, status)) {
        std::puts("HTTP failed");
        return;
    }
    std::printf("status code: %d\ncontent: ", status);
    std::fwrite(content.data(), 1, content.size(), stdout);
    std::putchar('\n');
    std::printf("HTTP %d\n", status);
}
