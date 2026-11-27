// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode2_base.hpp"

using namespace atoms3_qrcode2;

namespace {
constexpr uint8_t automaticMode[] = {0x21, 0x61, 0x41, 0x02};
constexpr uint8_t m14Ack[] = {0x22, 0x61, 0x41, 0x02, 0x00};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    atoms3_qrcode::drawScreen("scanning", TFT_GREEN);
    const bool configured = initScanner() && exchange("AUTO", automaticMode, m14Ack, 200);
    if (!configured) {
        atoms3_qrcode::drawRow(25, "Scan error", TFT_RED);
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
