// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../nbiot_mqtt.hpp"
#include "../sim7028.hpp"

extern "C" void app_main()
{
    using namespace sim7028;
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    nbiot_mqtt::label("label0", 8, 7);
    std::puts("label0");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    bool attached = false;
    while (true) {
        if (!attached) attached = attach();
        if (attached && dial("mqtt.m5stack.com", 1883)) {
            nbiot_mqtt::session(send, pendingBytes, socketRead, 8, 7);
            closeSocket();
            std::puts("MQTT session ended; retrying");
        } else {
            attached = false;
            std::puts("Modem/PDP or TCP unavailable; retrying");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
