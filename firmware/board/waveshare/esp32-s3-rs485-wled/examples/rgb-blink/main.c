// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_pixel.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_pixel_blink(2, 1);
}
