// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr char mountPoint[] = "/notes";
constexpr char directory[] = "/notes/M5Stack";
constexpr char path[] = "/notes/M5Stack/notes.txt";
constexpr char suffix[] = "This is Appended Info.\n";

bool dataIsErased()
{
    const auto* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                ESP_PARTITION_SUBTYPE_ANY, "data");
    if (!part) return false;

    const void* mapping = nullptr;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &mapping, &handle) != ESP_OK) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(mapping);
    const bool erased = std::all_of(bytes, bytes + part->size,
                                    [](uint8_t byte) { return byte == 0xff; });
    esp_partition_munmap(handle);
    return erased;
}

bool mountData()
{
    esp_vfs_littlefs_conf_t config{};
    config.base_path = mountPoint;
    config.partition_label = "data";
    auto status = esp_vfs_littlefs_register(&config);
    // A non-erased volume may contain user files even when mounting fails.
    if (status == ESP_FAIL && dataIsErased()) {
        std::puts("Initializing erased LittleFS data partition");
        config.format_if_mount_failed = true;
        status = esp_vfs_littlefs_register(&config);
    }
    if (status != ESP_OK) {
        std::printf("LittleFS mount failed: %s\n", esp_err_to_name(status));
        return false;
    }
    std::puts("LittleFS data mounted");
    return true;
}

void appendOrCreate()
{
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) {
        std::perror("M5Stack directory");
        return;
    }

    const int fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        if (errno != ENOENT) {
            std::perror("Open note");
            return;
        }
        std::puts("NOT FOUND.");
        std::puts("/M5Stack/notes.txt is creating.");
        const int created = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
        if (created < 0 || close(created) != 0) {
            std::perror("Create note");
            return;
        }
        std::puts("Empty note created");
        return;
    }

    std::puts("FOUND.");
    std::puts("/M5Stack/notes.txt");
    const auto written = write(fd, suffix, sizeof(suffix) - 1);
    const int closed = close(fd);
    if (written != sizeof(suffix) - 1 || closed != 0) {
        std::puts("Append failed");
        return;
    }
    std::puts("Finished Appending data to SPIFFS");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    if (mountData()) appendOrCreate();
}
