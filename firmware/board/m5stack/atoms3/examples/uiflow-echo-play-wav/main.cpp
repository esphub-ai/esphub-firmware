// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "../echo_wav.hpp"

extern "C" void app_main()
{
    using namespace echo_wav;
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_display_value = 0;
    M5.begin(config);
    M5.Speaker.end();
    M5.Mic.end();
    unsigned cursor = 0;
    if (initCodec()) {
        show("Playing", cursor);
        const bool complete = playTo(cursor, totalFrames);
        show(complete ? "Complete" : "Echo unavailable", cursor);
    } else {
        show("Echo unavailable", cursor);
    }
    for (;;) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
