// Adapted from the MIT-licensed M5AtomS3 SPIFFS write-and-read example.
// The EspHub data partition is LittleFS and must already be initialized.
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

namespace {
constexpr char mountPoint[] = "/notes";
constexpr char directory[] = "/notes/atoms3-storage";
constexpr char filePath[] = "/notes/atoms3-storage/notes.txt";
constexpr char greeting[] = "Hello IOT World.\n";

void show(const char* status, const char* content = nullptr)
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

bool prepareFile()
{
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) {
        show("Directory error");
        return false;
    }

    // O_EXCL preserves a file from an earlier boot or another application.
    const int fd = open(filePath, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        if (errno == EEXIST) {
            show("Existing note kept", "Press A to read");
            return true;
        }
        show("Create failed");
        return false;
    }
    const auto written = write(fd, greeting, sizeof(greeting) - 1);
    const int closed = close(fd);
    if (written != sizeof(greeting) - 1 || closed != 0) {
        unlink(filePath);
        show("Write failed");
        return false;
    }

    show("Note written", "Press A to read");
    std::printf("Wrote %zu bytes to %s\n", sizeof(greeting) - 1, filePath);
    return true;
}

void readFile()
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
}

extern "C" void app_main()
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
        return;
    }

    const bool ready = prepareFile();
    for (;;) {
        M5.update();
        if (ready && M5.BtnA.wasPressed()) {
            readFile();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
