// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace {
constexpr const char* mountPath = "/sd";
constexpr size_t bufferSize = 1024;
std::array<std::array<int16_t, bufferSize / 2>, 2> buffers;
std::array<std::atomic<bool>, 2> busy{};
std::atomic<unsigned> releases{0};

void show(const char* message, const char* path = "")
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setCursor(4, 12);
    M5.Display.println(message);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(4, 42);
    M5.Display.print(path);
    std::puts(message);
}

void released(void*, const void* data, uint8_t)
{
    for (unsigned i = 0; i < buffers.size(); ++i) {
        if (data == buffers[i].data()) {
            releases.fetch_add(1, std::memory_order_relaxed);
            busy[i].store(false, std::memory_order_release);
        }
    }
}

uint16_t le16(const uint8_t* p) { return p[0] | unsigned(p[1]) << 8; }
uint32_t le32(const uint8_t* p) { return le16(p) | uint32_t(le16(p + 2)) << 16; }

struct Wav {
    uint32_t rate = 0;
    uint32_t bytes = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t align = 0;
};

bool parseWav(FILE* file, Wav& wav)
{
    if (std::fseek(file, 0, SEEK_END) != 0) return false;
    const long fileSize = std::ftell(file);
    if (fileSize < 12 || std::fseek(file, 0, SEEK_SET) != 0) return false;
    uint8_t header[12];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header)
        || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
    const uint64_t end = uint64_t(le32(header + 4)) + 8;
    if (end > uint64_t(fileSize)) return false;

    for (uint64_t pos = 12; pos + 8 <= end;) {
        uint8_t chunk[8];
        if (std::fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) return false;
        const auto len = le32(chunk + 4);
        const auto next = pos + 8 + len + (len & 1u);
        if (next > end) return false;
        if (!std::memcmp(chunk, "fmt ", 4) && !wav.rate) {
            uint8_t fmt[16];
            if (len < sizeof(fmt) || std::fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) return false;
            wav.channels = le16(fmt + 2);
            wav.rate = le32(fmt + 4);
            wav.align = le16(fmt + 12);
            wav.bits = le16(fmt + 14);
            if (le16(fmt) != 1 || wav.channels < 1 || wav.channels > 2
                || (wav.bits != 8 && wav.bits != 16) || wav.align != wav.channels * (wav.bits / 8)
                || wav.rate < 1000 || wav.rate > 192000 || le32(fmt + 8) != wav.rate * wav.align)
                return false;
        } else if (!std::memcmp(chunk, "data", 4)) {
            wav.bytes = len;
            return wav.rate && len && len % wav.align == 0;
        }
        if (std::fseek(file, long(next), SEEK_SET) != 0) return false;
        pos = next;
    }
    return false;
}

bool waitBuffer(unsigned index)
{
    const auto deadline = M5.millis() + 1500;
    while (busy[index].load(std::memory_order_acquire)) {
        if (int32_t(deadline - M5.millis()) <= 0) return false;
        vTaskDelay(1);
    }
    return true;
}

bool streamWav(FILE* file, const Wav& wav)
{
    unsigned index = 0;
    unsigned blocks = 0;
    bool ok = true;
    auto& speaker = M5.Speaker;
    if (!speaker.begin()) return false;
    releases.store(0);
    for (auto remaining = wav.bytes; remaining;) {
        if (!waitBuffer(index)) { ok = false; break; }
        const auto count = std::min<size_t>(remaining, bufferSize);
        auto* data = buffers[index].data();
        if (std::fread(data, 1, count, file) != count) { ok = false; break; }
        busy[index].store(true, std::memory_order_release);
        const bool stereo = wav.channels == 2;
        const bool accepted = wav.bits == 16
            ? speaker.playRaw(data, count / sizeof(*data), wav.rate, stereo, 1, 0)
            : speaker.playRaw(reinterpret_cast<const uint8_t*>(data), count, wav.rate, stereo, 1, 0);
        if (!accepted) {
            busy[index].store(false, std::memory_order_release);
            ok = false;
            break;
        }
        ++blocks;
        remaining -= count;
        index ^= 1;
    }
    // Pad the queued audio so final I2S word alignment cannot cut source data.
    static constexpr int16_t pad[4]{};
    if (ok) ok = speaker.playRaw(pad, wav.channels * 2, wav.rate, wav.channels == 2, 1, 0);
    if (ok) ok = waitBuffer(0) && waitBuffer(1);
    const auto deadline = M5.millis() + 1500;
    while (ok && speaker.isPlaying()) {
        if (int32_t(deadline - M5.millis()) <= 0) ok = false;
        vTaskDelay(1);
    }
    if (ok) M5.delay(65);
    speaker.end();
    for (auto& flag : busy) flag.store(false, std::memory_order_release);
    std::printf("Buffers submitted=%u released=%u\n", blocks, releases.load());
    return ok && releases.load() == blocks;
}

unsigned playDirectory()
{
    auto* dir = opendir(mountPath);
    if (!dir) return 0;
    unsigned played = 0;
    while (const auto* entry = readdir(dir)) {
        const char* name = entry->d_name;
        const auto length = std::strlen(name);
        if (length < 5 || strcasecmp(name + length - 4, ".wav") != 0) continue;
        char path[sizeof(entry->d_name) + 5];
        std::snprintf(path, sizeof(path), "%s/%s", mountPath, name);
        struct stat info{};
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        auto* file = std::fopen(path, "rb");
        if (!file) continue;
        Wav wav;
        if (!parseWav(file, wav)) {
            std::printf("Skip invalid WAV: %s\n", path);
        } else {
            show("Playing", path);
            std::printf("File %s rate=%u bits=%u channels=%u frames=%u\n",
                        path, unsigned(wav.rate), unsigned(wav.bits), unsigned(wav.channels), unsigned(wav.bytes / wav.align));
            const bool ok = streamWav(file, wav);
            std::printf("%s: %s\n", ok ? "Playback complete" : "Playback failed", path);
            played += ok;
            M5.delay(500);
        }
        std::fclose(file);
        M5.update();
    }
    closedir(dir);
    return played;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_speaker.atomic_spk = true;
    config.external_display_value = 0;
    config.internal_imu = false;
    config.internal_rtc = false;
    M5.begin(config);
    M5.In_I2C.release();
    auto speaker = M5.Speaker.config();
    if (speaker.pin_bck != 5 || speaker.pin_ws != 39 || speaker.pin_data_out != 38) {
        show("Atomic SPK missing");
        return;
    }
    speaker.i2s_port = I2S_NUM_0;
    speaker.stereo = true;
    speaker.magnification = 1;
    M5.Speaker.config(speaker);
    M5.Speaker.setVolume(255);
    M5.Speaker.setAllChannelVolume(255);
    M5.Speaker.setBufferReleaseCallback(nullptr, released);

    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.miso_io_num = GPIO_NUM_8;
    bus.mosi_io_num = GPIO_NUM_6;
    bus.quadwp_io_num = bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) {
        show("SD SPI init failed");
        return;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SDSPI_SLOT_NO_CS; // Atomic SPK Base pulls card CS low.
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 3;
    show("SD WAV player");
    std::puts("Atomic SPK I2S0 BCLK=5 WS=39 DOUT=38; SPI2 SCK=7 MISO=8 MOSI=6 CS=none");
    for (;;) {
        sdmmc_card_t* card = nullptr;
        const auto mounted = esp_vfs_fat_sdspi_mount(mountPath, &host, &device, &options, &card);
        if (mounted != ESP_OK) {
            show("no SD card");
            std::printf("SD mount: %s\n", esp_err_to_name(mounted));
        } else {
            std::printf("SD mounted internal_free=%u\n", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            if (!playDirectory()) show("no *.wav found");
            esp_vfs_fat_sdcard_unmount(mountPath, card);
        }
        M5.update();
        M5.delay(1000);
    }
}
