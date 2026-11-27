// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode2_base.hpp"

using namespace atoms3_qrcode2;

namespace {
constexpr uint8_t continuousMode[] = {0x21, 0x61, 0x41, 0x01};
constexpr uint8_t continuousAck[] = {0x22, 0x61, 0x41, 0x01, 0x00};
constexpr uint8_t startupTone[] = {0x21, 0x63, 0x45, 0x01};
constexpr uint8_t startupToneAck[] = {0x22, 0x63, 0x45, 0x01};
constexpr uint8_t successBeep[] = {0x21, 0x63, 0x42, 0x02};
constexpr uint8_t successBeepAck[] = {0x22, 0x63, 0x42, 0x02};
constexpr uint8_t startDecode[] = {0x32, 0x75, 0x01};
constexpr uint8_t stopDecode[] = {0x32, 0x75, 0x02};
constexpr uint8_t stopAck[] = {0x33, 0x75, 0x02, 0x00, 0x00};

enum class Scan { stopped, scanning, unknown };

Scan showScan(Scan state)
{
    switch (state) {
        case Scan::stopped: drawRow(25, "stop scan", TFT_WHITE); break;
        case Scan::scanning: drawRow(25, "scanning", TFT_GREEN); break;
        case Scan::unknown: drawRow(25, "Scan error", TFT_RED); break;
    }
    return state;
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen("stop scan", TFT_WHITE);

    const bool configured = initScanner()
        && exchange("MODE", continuousMode, continuousAck, 200)
        && exchange("TONE", startupTone, startupToneAck, 150)
        && exchange("BEEP", successBeep, successBeepAck, 150)
        && exchange("INIT STOP", stopDecode, stopAck, 150);
    if (!configured) {
        showScan(Scan::unknown);
        std::puts("QRCode2 scanner unavailable");
    }

    Scan scan = Scan::stopped;
    Receiver receiver;
    for (;;) {
        M5.update();
        if (configured && M5.BtnA.wasClicked()) {
            receiver.reset();
            if (scan == Scan::stopped) {
                const bool sent = writeCommand(startDecode);
                std::puts(sent ? "QRCode2 START sent" : "QRCode2 START write failed");
                scan = showScan(sent ? Scan::scanning : Scan::unknown);
            } else {
                // A rejected STOP leaves the module state unknown. Click again to retry it.
                scan = showScan(exchange("STOP", stopDecode, stopAck, 150) ? Scan::stopped : Scan::unknown);
            }
        }
        if (configured && scan == Scan::scanning) {
            receiver.poll();
        }
        vTaskDelay(1);
    }
}
