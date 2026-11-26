// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "EPD_13in3e.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_epaper_begin();
    EPD_13IN3E_Init();
    EPD_13IN3E_Show6Block();
    EPD_13IN3E_Sleep();
    example_output(GPIO_NUM_1, 0); // Disconnect the display rail after refresh.
}
