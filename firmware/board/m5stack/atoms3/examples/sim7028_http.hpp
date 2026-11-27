// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once

#include "sim7028.hpp"
#include <algorithm>
#include <cstdlib>
#include <strings.h>

namespace sim7028 {
constexpr size_t responseLimit = 8192;
constexpr char body[] = R"({"message":"Hello from M5Stack!","status":"active"})";

inline bool readHttp(std::string& http, int& status)
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
        if (!socketRead(std::min(available, chunkSize), http, closed, responseLimit)) return false;
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

inline bool post(std::string& content, int& status)
{
    if (!dial("httpbin.org", 80)) return false;

    std::string request =
        "POST /post HTTP/1.0\r\nHost: httpbin.org\r\nContent-Type: application/json\r\n"
        "Custom-Header: MyHeaderValue\r\nContent-Length: ";
    request += std::to_string(sizeof(body) - 1);
    request += "\r\nConnection: close\r\n\r\n";
    request += body;
    const bool result = sim7028::send(request) && readHttp(content, status);
    closeSocket();
    return result;
}
}
