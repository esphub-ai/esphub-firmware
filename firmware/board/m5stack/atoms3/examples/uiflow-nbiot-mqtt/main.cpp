// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../nbiot_mqtt.hpp"

namespace {
using namespace sim7020;
void show(const char* status) { std::puts(status); }

bool dial()
{
    std::string dns;
    if (command("AT+CDNSGIP=mqtt.m5stack.com", "+CDNSGIP: 1,", 10000, &dns) != Reply::ok) return false;
    char ip[16];
    if (std::sscanf(dns.c_str(), "+CDNSGIP: 1,\"mqtt.m5stack.com\",\"%15[0-9.]\"", ip) != 1 ||
        !nonzeroIpv4(ip)) return false;
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSTART=0,\"TCP\",\"%s\",\"1883\"", ip);
    return command(cmd, "CONNECT OK", 160000) == Reply::ok;
}

bool send(std::string_view data)
{
    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%u", unsigned(data.size()));
    return command(cmd, ">") == Reply::ok && write(data) && await("SEND OK", 3000) == Reply::ok;
}

bool pendingBytes(unsigned& available, bool& closed)
{
    std::string row;
    return command("AT+CIPRXGET=4,0", "+CIPRXGET: 4,0,", 3000, &row, &closed) == Reply::ok &&
           std::sscanf(row.c_str(), "+CIPRXGET: 4,0,%u", &available) == 1;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    nbiot_mqtt::label("label0", 4, 6);
    show("label0");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    bool attached = false;
    while (true) {
        if (!attached) attached = attach(show);
        if (attached && dial()) {
            nbiot_mqtt::session(send, pendingBytes, receive, 4, 6);
            command("AT+CIPCLOSE=0", "CLOSE OK");
            show("MQTT session ended; retrying");
        } else {
            attached = false;
            show("Modem/PDP or TCP unavailable; retrying");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
