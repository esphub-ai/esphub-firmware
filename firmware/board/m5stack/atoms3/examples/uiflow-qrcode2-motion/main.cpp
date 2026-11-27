// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode2_base.hpp"

using namespace atoms3_qrcode2;

namespace {
constexpr uint8_t motionMode[] = {0x21, 0x61, 0x41, 0x05};
constexpr uint8_t motionAck[] = {0x22, 0x61, 0x41, 0x05, 0x00};
constexpr uint8_t sensitivity[] = {0x21, 0x61, 0x44, 0x01};
// The pinned QRCodeM14 driver expects a status byte without a level echo.
constexpr uint8_t sensitivityAck[] = {0x22, 0x61, 0x44, 0x00};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("detecting", TFT_GREEN);

    const bool configured = initScanner()
        && exchange("MODE", motionMode, motionAck, 200)
        && exchange("SENSITIVITY", sensitivity, sensitivityAck, 200);
    if (!configured) {
        drawRow(25, "Scan error", TFT_RED);
        std::puts("QRCode2 scanner unavailable");
    }

    Receiver receiver;
    for (;;) {
        M5.update();
        if (configured) {
            receiver.poll();
        }
        vTaskDelay(1);
    }
}
