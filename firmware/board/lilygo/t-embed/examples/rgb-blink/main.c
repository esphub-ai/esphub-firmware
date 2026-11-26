// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_apa102.h"
#include "example_gpio.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_46, 1);
    example_apa102_blink(42, 45, 7);
}
