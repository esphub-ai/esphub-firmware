// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_pcf85063.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_read_clock(GPIO_NUM_15, GPIO_NUM_14);
}
