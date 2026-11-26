// Adapted from the MIT-licensed M5AtomS3 Storage Counter example.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Advanced/Storage/Counter/Counter.ino
#include "eh_agent.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <M5Unified.h>
#include <cinttypes>
#include <cstdio>

namespace {
esp_err_t incrementCounter(uint32_t& previous, uint32_t& current)
{
    nvs_handle_t handle;
    esp_err_t status = nvs_open("atoms3_counter", NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }

    status = nvs_get_u32(handle, "counter", &previous);
    if (status == ESP_OK || status == ESP_ERR_NVS_NOT_FOUND) {
        current = previous + 1;
        status = nvs_set_u32(handle, "counter", current);
        if (status == ESP_OK) {
            status = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    return status;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

    auto& display = M5.Display;
    display.fillScreen(BLACK);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(8, 20);
    display.println("Storage Counter");

    uint32_t previous = 0, current = 0;
    const auto status = incrementCounter(previous, current);
    if (status != ESP_OK) {
        display.setCursor(8, 47);
        display.println("NVS write failed");
        std::printf("Counter NVS error: %s\n", esp_err_to_name(status));
        return;
    }
    display.setCursor(8, 47);
    display.printf("Count: %" PRIu32 "\n", current);
    display.setCursor(8, 78);
    display.println("Restart in 10 s");
    std::printf("Counter committed: previous=%" PRIu32 " current=%" PRIu32 "\n", previous, current);

    vTaskDelay(pdMS_TO_TICKS(10000));
    std::puts("Counter: restarting");
    esp_restart();
}
