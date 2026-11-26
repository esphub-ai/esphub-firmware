// SPDX-License-Identifier: MIT
#include "eh_agent.h"

extern "C" void factory_demo_main();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    factory_demo_main();
}
