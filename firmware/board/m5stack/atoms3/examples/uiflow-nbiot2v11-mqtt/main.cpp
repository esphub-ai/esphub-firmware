// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../nbiot2v11_startup.hpp"
#include "../nbiot_mqtt.hpp"
#include "../sim7028.hpp"

extern "C" void app_main()
{
    using namespace sim7028;
    constexpr int labelX = 1;
    constexpr int labelY = 5;
    constexpr nbiot_mqtt::SessionOptions mqttOptions{.cleanSession = true, .keepaliveSeconds = 120};
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    nbiot_mqtt::label("label0", labelX, labelY);
    std::puts("label0");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (nbiot2v11::startModemPower()) {
        bool attached = false;
        while (true) {
            if (!attached) attached = nbiot2v11::waitForPdp();
            if (attached && dial("mqtt.m5stack.com", 1883)) {
                nbiot_mqtt::session(send, pendingBytes, socketRead, labelX, labelY, mqttOptions);
                closeSocket();
                std::puts("MQTT session ended; retrying");
            } else {
                attached = false;
                std::puts("Modem/PDP or TCP unavailable; retrying");
            }
            M5.update();
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    std::puts("M5IOE1 initialization failed; modem not contacted");
    while (true) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
