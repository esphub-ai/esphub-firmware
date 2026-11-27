// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../qrcode_base.hpp"

using namespace atoms3_qrcode;

namespace {
constexpr uint8_t hostMode[] = {0x07, 0xc6, 0x04, 0x08, 0x00, 0x8a, 0x08, 0xfe, 0x95};
constexpr uint8_t positionLight[] = {0x08, 0xc6, 0x04, 0x08, 0x00, 0xf2, 0x03, 0x00, 0xfe, 0x31};
constexpr uint8_t fillLight[] = {0x08, 0xc6, 0x04, 0x08, 0x00, 0xf2, 0x02, 0x00, 0xfe, 0x32};
constexpr uint8_t startDecode[] = {0x04, 0xe4, 0x04, 0x00, 0xff, 0x14};
constexpr uint8_t stopDecode[] = {0x04, 0xe5, 0x04, 0x00, 0xff, 0x13};

enum class Scan { stopped, scanning, unknown };

template <size_t length>
bool exchange(const char* name, const uint8_t (&command)[length], int timeoutMs)
{
    const auto ack = sendCommand(command, timeoutMs);
    std::printf("QRCode %s %s\n", name, ackText(ack));
    return ack == Ack::accepted;
}

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

    const bool ready = wakeScanner() && setMode("HOST", hostMode)
        && exchange("POS", positionLight, 150) && exchange("FILL", fillLight, 150);
    if (!ready) {
        showScan(Scan::unknown);
        std::puts("QRCode scanner unavailable");
    }

    auto scan = Scan::stopped;
    bool waitForDoneLow = false;
    for (;;) {
        M5.update();
        if (ready && M5.BtnA.wasClicked()) {
            if (scan == Scan::stopped) {
                scan = showScan(exchange("START", startDecode, 100) ? Scan::scanning : Scan::unknown);
            } else {
                // A missing ACK leaves the module's state unknown. Recover with STOP.
                scan = showScan(exchange("STOP", stopDecode, 100) ? Scan::stopped : Scan::unknown);
            }
        }
        if (!gpio_get_level(done)) {
            if (waitForDoneLow) {
                uart_flush_input(port);
                waitForDoneLow = false;
            }
        } else if (ready && scan == Scan::scanning && !waitForDoneLow) {
            const auto frame = readFrame();
            waitForDoneLow = frame == Frame::timeout;
            if (frame == Frame::displayed && !exchange("REARM", startDecode, 100)) {
                scan = showScan(Scan::unknown);
            }
        }
        vTaskDelay(1);
    }
}
