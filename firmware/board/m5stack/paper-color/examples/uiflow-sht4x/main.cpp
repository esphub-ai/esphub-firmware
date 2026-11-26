// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../sht4x.hpp"
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);

    for (;;) {
        M5.update();
        float temperature = 0;
        float humidity = 0;
        if (!paper_color::sht4x::sample(temperature, humidity)) {
            std::puts("SHT4X reading unavailable");
            return;
        }
        std::printf("hum:%.2f\n", humidity);

        // Each UiFlow2 getter starts a fresh conversion.
        if (!paper_color::sht4x::sample(temperature, humidity)) {
            std::puts("SHT4X reading unavailable");
            return;
        }
        std::printf("temp:%.2f\n", temperature);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
