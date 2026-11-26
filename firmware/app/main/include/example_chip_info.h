// SPDX-License-Identifier: MIT
#pragma once

#include <inttypes.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void example_chip_info(const char *board)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes;
    ESP_ERROR_CHECK(esp_flash_get_physical_size(NULL, &flash_bytes));
    for (;;) {
        ESP_LOGI("board", "%s | ESP32-S3 rev %u.%u | %" PRIu32 " MiB flash | Wi-Fi %s | BLE %s",
                 board, chip.revision / 100, chip.revision % 100, flash_bytes / (1024u * 1024u),
                 (chip.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
                 (chip.features & CHIP_FEATURE_BLE) ? "yes" : "no");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
