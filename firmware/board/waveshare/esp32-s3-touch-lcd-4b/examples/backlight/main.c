// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_gpio.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_blink(GPIO_NUM_4, 1);
}
