// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_apa102.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_apa102_blink(38, 39, 1);
}
