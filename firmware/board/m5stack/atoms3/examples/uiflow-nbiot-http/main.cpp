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
        if (!receive(std::min(available, chunkSize), http, closed, responseLimit)) return false;
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
    if (!attach(show)) {
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
