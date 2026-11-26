// SPDX-License-Identifier: MIT
#include "eh_agent.h"

extern "C" void stopwatch_factory_main();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    stopwatch_factory_main();
}
