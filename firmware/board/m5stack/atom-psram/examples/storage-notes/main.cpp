// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr auto button = GPIO_NUM_39;
constexpr char mountPoint[] = "/notes";
constexpr char directory[] = "/notes/M5Stack";
constexpr char path[] = "/notes/M5Stack/notes.txt";
constexpr char greeting[] = "Hello IOT World.\n";

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

bool prepareNote()
{
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) {
        std::perror("M5Stack directory");
        return false;
    }
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        if (errno == EEXIST) {
            std::puts("Existing note kept: /M5Stack/notes.txt");
            return true;
        }
        std::perror("Create note");
        return false;
    }

    const auto size = write(fd, greeting, sizeof(greeting) - 1);
    const int closed = close(fd);
    if (size != sizeof(greeting) - 1 || closed != 0) {
        unlink(path);
        std::puts("Note write failed");
        return false;
    }
    std::puts("Wrote /M5Stack/notes.txt: Hello IOT World.");
    return true;
}

void readNote()
{
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        std::puts(errno == ENOENT ? "NOT FOUND. /M5Stack/notes.txt" : "Note open failed");
        return;
    }
    std::puts("FOUND. /M5Stack/notes.txt");
    char bytes[128];
    for (size_t size; (size = std::fread(bytes, 1, sizeof(bytes), file)) != 0;) {
        std::fwrite(bytes, 1, size, stdout);
    }
    const bool failed = std::ferror(file);
    std::fclose(file);
    std::puts(failed ? "\nNote read failed" : "\nNote read complete");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    if (!mountData() || !prepareNote()) return;

    // GPIO39 is input-only with an external pull-up on Atom-PSRAM.
    ESP_ERROR_CHECK(gpio_set_direction(button, GPIO_MODE_INPUT));
    std::puts("Press GPIO39 button to read the note");
    bool previous = false;
    for (;;) {
        const bool pressed = gpio_get_level(button) == 0;
        if (pressed != previous) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (pressed == (gpio_get_level(button) == 0)) {
                previous = pressed;
                if (pressed) readNote();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
