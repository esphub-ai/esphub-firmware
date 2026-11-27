// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode2_base.hpp"

using namespace atoms3_qrcode2;

namespace {
constexpr uint8_t pulseMode[] = {0x21, 0x61, 0x41, 0x04};
constexpr uint8_t pulseAck[] = {0x22, 0x61, 0x41, 0x04, 0x00};
constexpr int pulseMs = 20;
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("stop scan", TFT_WHITE);

    const bool configured = initScanner() && exchange("PULSE", pulseMode, pulseAck, 200);
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode2 scanner unavailable");
    }

    bool scanning = false;
    Receiver receiver;
    for (;;) {
        M5.update();
        if (configured) {
            if (M5.BtnA.wasClicked()) {
                gpio_set_level(trigger, 0);
                vTaskDelay(pdMS_TO_TICKS(pulseMs));
                gpio_set_level(trigger, 1);
                if (!scanning) {
                    drawRow(25, "scanning", TFT_GREEN);
                    scanning = true;
                }
            }
            if (receiver.poll() && scanning) {
                drawRow(25, "stop scan", TFT_WHITE);
                scanning = false;
            }
        }
        vTaskDelay(1);
    }
}
