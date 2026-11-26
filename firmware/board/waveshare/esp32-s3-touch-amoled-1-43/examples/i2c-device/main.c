// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "example_i2c.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    i2c_master_bus_handle_t bus = example_i2c_bus(GPIO_NUM_47, GPIO_NUM_48);
    for (;;) {
        esp_err_t status = i2c_master_probe(bus, 0x6b, 100);
        ESP_LOGI("board", "QMI8658 at 0x6b: %s", esp_err_to_name(status));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
