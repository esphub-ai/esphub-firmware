// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "esp_display_panel.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = esp_panel::board::Board().getConfig();
    config.touch.reset(); // This SKU omits the touch controller.
    static esp_panel::board::Board board(config);
    ESP_ERROR_CHECK(board.init() && board.begin() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(board.getLCD()->colorBarTest() ? ESP_OK : ESP_FAIL);
}
