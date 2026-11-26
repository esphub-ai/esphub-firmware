// SPDX-License-Identifier: MIT
#include "eh_agent.h"

extern "C" void paper_color_factory_main();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    paper_color_factory_main();
}
