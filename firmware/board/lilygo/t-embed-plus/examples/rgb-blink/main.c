// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_pixel.h"
#include "example_gpio.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_output(GPIO_NUM_15, 1);
    example_pixel_blink(14, 8);
}
