// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_i2c.h"
#include "example_pixel.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    i2c_master_bus_handle_t bus = example_i2c_bus(GPIO_NUM_3, GPIO_NUM_2);
    i2c_master_dev_handle_t expander = example_i2c_device(bus, 0x22);
    example_update_bits(expander, 0x02, 0x08, 0x08); // P03 output high
    example_update_bits(expander, 0x06, 0x08, 0);    // P03 output enabled
    example_pixel_blink(14, 8);
}
