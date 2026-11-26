// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <cstdint>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "hal/hal.h"

static bool is_erased(const esp_partition_t &partition)
{
    std::array<uint8_t, 1024> buffer;
    for (size_t offset = 0; offset < partition.size; offset += buffer.size()) {
        size_t length = std::min<size_t>(buffer.size(), partition.size - offset);
        if (esp_partition_read(&partition, offset, buffer.data(), length) != ESP_OK ||
            !std::all_of(buffer.begin(), buffer.begin() + length, [](uint8_t byte) { return byte == 0xff; })) {
            return false;
        }
    }
    return true;
}

void Hal::fs_init()
{
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/spiflash";
    conf.partition_label = "data";
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_FAIL) {
        const auto *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, conf.partition_label);
        if (partition && is_erased(*partition)) {
            conf.format_if_mount_failed = true;
            err = esp_vfs_littlefs_register(&conf);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW("StopWatch", "Badge storage unavailable: %s", esp_err_to_name(err));
    }
}
