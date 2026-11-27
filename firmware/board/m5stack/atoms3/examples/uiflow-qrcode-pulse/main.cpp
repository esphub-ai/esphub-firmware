// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode_base.hpp"

using namespace atoms3_qrcode;

namespace {
constexpr uint8_t pulseMode[] = {0x07, 0xc6, 0x04, 0x08, 0x00, 0x8a, 0x02, 0xfe, 0x9b};
constexpr int pulseMs = 20;
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("stop scan", TFT_WHITE);

    const bool configured = wakeScanner() && setMode("PULSE", pulseMode);
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode scanner unavailable");
    }

    bool scanning = false;
    bool waitForDoneLow = false;
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
            if (!gpio_get_level(done)) {
                if (waitForDoneLow) {
                    uart_flush_input(port);
                    waitForDoneLow = false;
                }
            } else if (!waitForDoneLow) {
                const auto frame = readFrame();
                waitForDoneLow = frame == Frame::timeout;
                if (frame == Frame::displayed && scanning) {
                    drawRow(25, "stop scan", TFT_WHITE);
                    scanning = false;
                }
            }
        }
        vTaskDelay(1);
    }
}
