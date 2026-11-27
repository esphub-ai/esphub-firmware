// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../sim7028_http.hpp"
#include <M5Unified.h>


extern "C" void app_main()
{
    using namespace sim7028;
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& screen = M5.Display;
    screen.fillScreen(TFT_BLACK);
    screen.setFont(&fonts::DejaVu18);
    screen.setTextColor(TFT_WHITE, 0x222222U);
    screen.fillRect(8, 7, screen.textWidth("label0"), screen.fontHeight(), 0x222222U);
    screen.drawString("label0", 8, 7);
    std::puts("label0");
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (!sim7028::attach()) {
        std::puts("Modem/PDP failed");
        return;
    }
    std::puts("HTTP POST");
    std::string content;
    int status = 0;
    if (!post(content, status)) {
        std::puts("HTTP failed");
        return;
    }
    std::printf("status code: %d\ncontent: ", status);
    std::fwrite(content.data(), 1, content.size(), stdout);
    std::putchar('\n');
    std::printf("HTTP %d\n", status);
}
