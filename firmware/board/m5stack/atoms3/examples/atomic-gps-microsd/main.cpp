// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicGPS / MicroSD example.
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr char mountPoint[] = "/sd";
constexpr auto cardBus = SPI2_HOST;
constexpr uint64_t mib = 1024 * 1024;
constexpr size_t blockSize = 512;
constexpr unsigned blockCount = mib / blockSize;

bool report(bool ok, const char* operation)
{
    std::printf("MicroSD %s %s\n", operation, ok ? "OK" : "FAILED");
    return ok;
}

bool listDirectory(const char* path, unsigned depth)
{
    DIR* dir = opendir(path);
    if (!dir) {
        return false;
    }
    std::printf("MicroSD list %s\n", path);
    while (const auto* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[256];
        const int length = std::snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat info{};
        if (length >= 0 && length < int(sizeof(child)) && stat(child, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                std::printf("  DIR %s\n", entry->d_name);
                if (depth) {
                    listDirectory(child, depth - 1);
                }
            } else {
                std::printf("  FILE %s %lld bytes\n", entry->d_name, static_cast<long long>(info.st_size));
            }
        }
    }
    return closedir(dir) == 0;
}

bool writeText(const char* path, const char* mode, const char* text)
{
    FILE* file = std::fopen(path, mode);
    if (!file) {
        return false;
    }
    const size_t bytes = std::strlen(text);
    const bool written = std::fwrite(text, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && written;
}

bool readText(const char* path)
{
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        return false;
    }
    char buffer[32]{};
    const size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
    constexpr char expected[] = "Hello World!\n";
    const bool match = bytes == sizeof(expected) - 1 && std::memcmp(buffer, expected, bytes) == 0;
    const bool closed = std::fclose(file) == 0;
    if (closed && match) {
        std::printf("MicroSD readback %s: %s", path, expected);
    }
    return closed && match;
}

bool benchmark(const char* path, bool writing)
{
    FILE* file = std::fopen(path, writing ? "wb" : "rb");
    if (!file) {
        return false;
    }
    uint8_t pattern[blockSize], received[blockSize];
    unsigned block = 0;
    const auto started = esp_timer_get_time();
    for (; block < blockCount; ++block) {
        for (size_t i = 0; i < blockSize; ++i) {
            pattern[i] = static_cast<uint8_t>(block + i);
        }
        const bool ok = writing
            ? std::fwrite(pattern, 1, blockSize, file) == blockSize
            : std::fread(received, 1, blockSize, file) == blockSize
                && std::memcmp(received, pattern, blockSize) == 0;
        if (!ok) {
            break;
        }
        if ((block + 1) % 32 == 0) {
            vTaskDelay(1);
        }
    }
    const bool closed = std::fclose(file) == 0;
    const auto elapsedMs = (esp_timer_get_time() - started) / 1000;
    std::printf("MicroSD benchmark %s %u bytes in %" PRId64 " ms\n",
                writing ? "write" : "verified read", unsigned(block * blockSize), elapsedMs);
    return closed && block == blockCount;
}

bool runDemo()
{
    char root[16];
    unsigned index = 0;
    for (; index < 100; ++index) {
        std::snprintf(root, sizeof(root), "%s/EH%06u", mountPoint, index);
        if (mkdir(root, 0700) == 0) {
            break;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    if (index == 100) {
        return false;
    }
    std::printf("MicroSD sandbox %s\n", root);
    char child[32], hello[32], renamed[32], timed[32];
    std::snprintf(child, sizeof(child), "%s/MYDIR", root);
    std::snprintf(hello, sizeof(hello), "%s/HELLO.TXT", root);
    std::snprintf(renamed, sizeof(renamed), "%s/FOO.TXT", root);
    std::snprintf(timed, sizeof(timed), "%s/TEST.BIN", root);

    bool passed = report(mkdir(child, 0700) == 0, "mkdir")
        && report(listDirectory(root, 2), "recursive list")
        && report(rmdir(child) == 0, "rmdir")
        && report(writeText(hello, "wb", "Hello "), "write")
        && report(writeText(hello, "ab", "World!\n"), "append")
        && report(readText(hello), "read")
        && report(std::rename(hello, renamed) == 0, "rename")
        && report(readText(renamed), "renamed read")
        && report(unlink(renamed) == 0, "delete")
        && benchmark(timed, true)
        && benchmark(timed, false);

    for (const char* file : {hello, renamed, timed}) {
        if (unlink(file) != 0 && errno != ENOENT) {
            passed = false;
        }
    }
    if (rmdir(child) != 0 && errno != ENOENT) {
        passed = false;
    }
    const bool removed = report(rmdir(root) == 0, "sandbox cleanup");
    return passed && removed;
}

void show(const char* status, const char* detail = "")
{
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSerifItalic12pt7b);
    display.drawString("MicroSD", display.width() / 2, 20);
    display.drawString(status, display.width() / 2, 60);
    display.drawString(detail, display.width() / 2, 100);
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Mounting");
    std::puts("MicroSD start SPI2 MOSI=6 SCK=7 MISO=8 no host CS");
    vTaskDelay(pdMS_TO_TICKS(250));

    // The base has no host CS. SPI2 is dedicated to it because the LCD uses SPI3.
    spi_bus_config_t bus{};
    bus.mosi_io_num = GPIO_NUM_6;
    bus.sclk_io_num = GPIO_NUM_7;
    bus.miso_io_num = GPIO_NUM_8;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    const esp_err_t busResult = spi_bus_initialize(cardBus, &bus, SDSPI_DEFAULT_DMA);
    if (busResult != ESP_OK) {
        std::printf("MicroSD SPI initialization failed: %s\n", esp_err_to_name(busResult));
        show("Mount failed");
        return;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = cardBus;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = cardBus;
    device.gpio_cs = SDSPI_SLOT_NO_CS;
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 4;
    sdmmc_card_t* card = nullptr;
    const esp_err_t mounted = esp_vfs_fat_sdspi_mount(mountPoint, &host, &device, &options, &card);
    if (mounted != ESP_OK) {
        std::printf("MicroSD mount failed: %s\n", esp_err_to_name(mounted));
        show("Mount failed");
        spi_bus_free(cardBus);
        return;
    }

    sdmmc_card_print_info(stdout, card);
    char usage[40]{};
    uint64_t total, available;
    if (esp_vfs_fat_info(mountPoint, &total, &available) == ESP_OK) {
        std::printf("MicroSD capacity used=%" PRIu64 " total=%" PRIu64 " bytes\n", total - available, total);
        std::snprintf(usage, sizeof(usage), "%" PRIu64 " / %" PRIu64 " MB", (total - available) / mib, total / mib);
    }
    show("Mounted", usage);
    std::puts("MicroSD mounted");
    const bool tested = report(listDirectory(mountPoint, 2), "root list") && runDemo();
    const bool unmounted = report(esp_vfs_fat_sdcard_unmount(mountPoint, card) == ESP_OK, "unmount");
    const bool released = report(spi_bus_free(cardBus) == ESP_OK, "bus release");
    show(tested && unmounted && released ? "Test passed" : "Test failed", usage);
    report(tested && unmounted && released, "demo");
}
