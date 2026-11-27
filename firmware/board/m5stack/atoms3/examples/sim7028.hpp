// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once

#include "sim7020.hpp"

namespace sim7028 {
using namespace sim7020;

// SIM7028 connect(apn) does not apply its APN argument. Configure the initial
// EPS context explicitly, using the SIM7028 AT manual's CID 0.
inline bool attach()
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

inline bool openNetwork()
{
    std::string row;
    if (command("AT+NETOPEN?", "+NETOPEN:", 3000, &row) != Reply::ok) return false;
    if (row == "+NETOPEN: 1") return true;
    return row == "+NETOPEN: 0" &&
           command("AT+NETOPEN", "+NETOPEN:", 30000, &row) == Reply::ok &&
           row == "+NETOPEN: 0";
}

inline bool readSocketLine(std::string& line, int64_t deadline, bool& closed)
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

inline bool socketRead(unsigned count, std::string& data, bool& closed, size_t limit)
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
        if (!received || received > count || data.size() + received > limit ||
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

inline bool pendingBytes(unsigned& amount, bool& closed)
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


inline bool dial(const char* host, unsigned port)
{
    char cmd[128];
    std::snprintf(cmd, sizeof(cmd), "AT+CDNSGIP=%s", host);
    std::string row;
    if (command(cmd, "+CDNSGIP: 1,", 10000, &row) != Reply::ok) return false;
    const std::string prefix = std::string("+CDNSGIP: 1,\"") + host + "\",\"";
    if (!row.starts_with(prefix) || !row.ends_with('"')) return false;
    row.pop_back();
    const char* ip = row.c_str() + prefix.size();
    if (!nonzeroIpv4(ip) || !openNetwork() || command("AT+CIPRXGET=1") != Reply::ok) return false;

    std::snprintf(cmd, sizeof(cmd), "AT+CIPOPEN=0,\"TCP\",\"%s\",\"%u\"", ip, port);
    return command(cmd, "+CIPOPEN:", 160000, &row) == Reply::ok && row == "+CIPOPEN: 0,0";
}

inline bool send(std::string_view data)
{
    char cmd[32], sent[40];
    const auto length = unsigned(data.size());
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%u", length);
    std::snprintf(sent, sizeof(sent), "+CIPSEND: 0,%u,%u", length, length);
    std::string reply;
    return command(cmd, ">") == Reply::ok && write(data) &&
           await("+CIPSEND:", 3000, &reply) == Reply::ok && reply == sent;
}

inline void closeSocket()
{
    std::string reply;
    command("AT+CIPCLOSE=0", "+CIPCLOSE:", 10000, &reply);
}
}
