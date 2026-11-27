// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../echo_wav.hpp"

extern "C" void app_main()
{
    using namespace echo_wav;
    constexpr unsigned pauseFrame = rate;
    static_assert(pauseFrame % dmaFrames == 0);
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_display_value = 0;
    M5.begin(config);
    M5.Speaker.end();
    M5.Mic.end();
    unsigned cursor = 0;
    if (initCodec()) {
        show("Playing", cursor);
        if (playTo(cursor, pauseFrame)) {
            show("Paused", cursor);
            vTaskDelay(pdMS_TO_TICKS(1000));
            show("Resuming", cursor);
            if (playTo(cursor, totalFrames)) {
                show("Complete", cursor);
                return;
            }
        }
    }
    show("Echo unavailable", cursor);
}
