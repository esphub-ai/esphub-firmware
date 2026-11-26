#pragma once

#include "eh_agent.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace storage {
constexpr char mountPoint[] = "/notes";
constexpr char directory[] = "/notes/atoms3-storage";
constexpr char filePath[] = "/notes/atoms3-storage/notes.txt";

inline void show(const char* status, const char* content = nullptr)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(BLACK);
    display.setCursor(6, 12);
    display.println("Storage notes");
    display.setCursor(6, 32);
    display.println(status);
    if (content) {
        display.setCursor(6, 55);
        display.print(content);
    }
    display.endWrite();
    std::printf("Storage notes: %s\n", status);
}

inline bool prepareDirectory()
{
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) {
        show("Directory error");
        return false;
    }

    return true;
}

inline void readFile()
{
    FILE* file = std::fopen(filePath, "r");
    if (!file) {
        show(errno == ENOENT ? "Note not found" : "Open failed");
        return;
    }
    char content[81]{};
    const size_t size = std::fread(content, 1, sizeof(content) - 1, file);
    const bool failed = std::ferror(file);
    std::fclose(file);
    if (failed) {
        show("Read failed");
        return;
    }

    show("Note found", content);
    std::printf("Read %zu bytes from %s\n%s", size, filePath, content);
}

inline bool start()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.setTextColor(WHITE, BLACK);

    esp_vfs_littlefs_conf_t config{};
    config.base_path = mountPoint;
    config.partition_label = "data";
    config.format_if_mount_failed = false;
    const esp_err_t status = esp_vfs_littlefs_register(&config);
    if (status != ESP_OK) {
        std::printf("LittleFS mount failed: %s\n", esp_err_to_name(status));
        show("DATA unavailable", "LittleFS required");
        return false;
    }

    return true;
}

inline void pollRead(bool ready)
{
    for (;;) {
        M5.update();
        if (ready && M5.BtnA.wasPressed()) {
            readFile();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
}
