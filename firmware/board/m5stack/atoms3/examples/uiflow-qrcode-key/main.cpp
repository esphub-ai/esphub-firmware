// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode_base.hpp"

using namespace atoms3_qrcode;

namespace {
constexpr uint8_t keyMode[] = {0x07, 0xc6, 0x04, 0x08, 0x00, 0x8a, 0x00, 0xfe, 0x9d};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("stop scan", TFT_WHITE);

    const bool ready = wakeScanner() && setMode("KEY", keyMode);
    if (!ready) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode scanner unavailable");
    }

    bool scanning = false;
    bool waitForDoneLow = false;
    for (;;) {
        M5.update();
        if (ready) {
            const bool pressed = M5.BtnA.isPressed();
            if (pressed != scanning) {
                gpio_set_level(trigger, pressed ? 0 : 1);
                scanning = pressed;
                drawRow(25, scanning ? "scanning" : "stop scan", scanning ? TFT_GREEN : TFT_WHITE);
            }
            // A result can finish arriving after BtnA is released.
            if (!gpio_get_level(done)) {
                if (waitForDoneLow) {
                    uart_flush_input(port);
                    waitForDoneLow = false;
                }
            } else if (!waitForDoneLow) {
                waitForDoneLow = readFrame() == Frame::timeout;
            }
        }
        vTaskDelay(1);
    }
}
