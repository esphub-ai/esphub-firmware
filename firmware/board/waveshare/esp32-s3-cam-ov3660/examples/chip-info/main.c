// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_chip_info.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_chip_info("ESP32-S3-CAM-OV3660");
}
