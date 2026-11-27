// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <cinttypes>
#include <cstdio>

namespace {
esp_err_t incrementCounter(uint32_t& count)
{
    nvs_handle_t handle;
    auto status = nvs_open("atomps_counter", NVS_READWRITE, &handle);
    if (status != ESP_OK) return status;

    status = nvs_get_u32(handle, "counter", &count);
    if (status == ESP_OK || status == ESP_ERR_NVS_NOT_FOUND) {
        status = nvs_set_u32(handle, "counter", ++count);
        if (status == ESP_OK) status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());

    uint32_t count = 0;
    const auto status = incrementCounter(count);
    if (status != ESP_OK) {
        std::printf("Counter NVS error: %s\n", esp_err_to_name(status));
        return;
    }
    std::printf("Current counter value: %" PRIu32 "\n", count);
    std::puts("Restarting in 10 seconds...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}
