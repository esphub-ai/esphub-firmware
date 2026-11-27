// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode2_base.hpp"

using namespace atoms3_qrcode2;

namespace {
constexpr uint8_t keyMode[] = {0x21, 0x61, 0x41, 0x00};
constexpr uint8_t keyAck[] = {0x22, 0x61, 0x41, 0x00, 0x00};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("stop scan", TFT_WHITE);

    const bool configured = initScanner() && exchange("KEY", keyMode, keyAck, 200);
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode2 scanner unavailable");
    }

    bool scanning = false;
    Receiver receiver;
    for (;;) {
        M5.update();
        if (configured) {
            const bool pressed = M5.BtnA.isPressed();
            if (pressed != scanning) {
                gpio_set_level(trigger, pressed ? 0 : 1);
                scanning = pressed;
                drawRow(25, scanning ? "scanning" : "stop scan", scanning ? TFT_GREEN : TFT_WHITE);
            }
            // Keep receiving results that arrive after BtnA is released.
            receiver.poll();
        }
        vTaskDelay(1);
    }
}
