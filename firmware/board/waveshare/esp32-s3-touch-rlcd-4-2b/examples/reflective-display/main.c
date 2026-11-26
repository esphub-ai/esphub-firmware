// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_st7305.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_st7305(GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_40, GPIO_NUM_5, GPIO_NUM_41);
}
