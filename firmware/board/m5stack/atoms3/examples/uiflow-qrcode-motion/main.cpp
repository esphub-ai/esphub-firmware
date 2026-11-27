// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode_base.hpp"

using namespace atoms3_qrcode;

namespace {
constexpr uint8_t motionSensingMode[] = {0x07, 0xc6, 0x04, 0x08, 0x00, 0x8a, 0x09, 0xfe, 0x94};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("detecting", TFT_GREEN);

    const bool configured = wakeScanner() && setMode("MOTION", motionSensingMode);
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode scanner unavailable");
    }

    bool waitForDoneLow = false;
    for (;;) {
        M5.update();
        if (configured) {
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
