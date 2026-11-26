// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "EPD_3in97.h"
#include "stdlib.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    example_epaper_begin();
    EPD_3IN97_Init();
    const unsigned stride = EPD_3IN97_WIDTH / 8;
    uint8_t *pixels = (uint8_t *)malloc(stride * EPD_3IN97_HEIGHT);
    ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_ERR_NO_MEM);
    for (unsigned y = 0; y < EPD_3IN97_HEIGHT; ++y) {
        for (unsigned x = 0; x < stride; ++x) pixels[y * stride + x] = (x / 8) % 2 ? 0xff : 0;
    }
    EPD_3IN97_Display(pixels);
    free(pixels);
    EPD_3IN97_Sleep();
}
