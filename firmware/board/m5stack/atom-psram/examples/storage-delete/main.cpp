// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "esp_littlefs.h"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    esp_vfs_littlefs_conf_t config{};
    config.base_path = "/notes";
    config.partition_label = "data";
    config.format_if_mount_failed = false;
    const auto status = esp_vfs_littlefs_register(&config);
    if (status != ESP_OK) {
        std::printf("LittleFS mount failed: %s\n", esp_err_to_name(status));
        return;
    }
    std::puts("LittleFS data mounted");

    constexpr char path[] = "/notes/M5Stack/notes.txt";
    struct stat note{};
    if (stat(path, &note) != 0) {
        if (errno == ENOENT) {
            std::puts("Note not found: /M5Stack/notes.txt");
        } else {
            std::perror("Note lookup failed");
        }
        return;
    }
    if (!S_ISREG(note.st_mode)) {
        std::puts("Not a regular file: /M5Stack/notes.txt");
        return;
    }
    if (unlink(path) != 0) {
        std::perror("Note delete failed");
        return;
    }
    std::puts("Note deleted: /M5Stack/notes.txt");
}
