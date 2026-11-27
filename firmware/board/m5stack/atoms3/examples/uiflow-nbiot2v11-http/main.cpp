// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../sim7028_http.hpp"
#include "../nbiot2v11_startup.hpp"

namespace {
void run()
{
    using namespace sim7020;
    initUart(modem, 5, 6, 4096);
    initUart(UART_NUM_1, 7, 8, 256);
    if (!nbiot2v11::startModemPower()) {
        std::puts("M5IOE1 initialization failed; modem not contacted");
        return;
    }
    if (!nbiot2v11::waitForPdp()) {
        std::puts("Modem/PDP failed");
        return;
    }
    std::puts("HTTP POST");
    std::string content;
    int status = 0;
    if (!sim7028::post(content, status)) {
        std::puts("HTTP failed");
        return;
    }
    std::printf("status code: %d\ncontent: ", status);
    std::fwrite(content.data(), 1, content.size(), stdout);
    std::printf("\nHTTP %d\n", status);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.fillScreen(TFT_BLACK);
    run();
    while (true) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
