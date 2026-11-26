// Behavior from M5Unified Advanced/Speaker_SD_wav_file (MIT, Copyright (c) 2021 M5Stack).
// https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/examples/Advanced/Speaker_SD_wav_file/Speaker_SD_wav_file.ino
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <strings.h>

namespace {
constexpr char mountPoint[] = "/sd";
constexpr int channel = 0;
// One buffer may be filled while the speaker has up to two queued.
alignas(4) std::array<std::array<int16_t, 1024>, 3> buffers;

bool mountCard()
{
    // A135 uses exclusive SPI with no host-controlled CS.
    // https://docs.m5stack.com/en/arduino/projects/atomic/atomic_tfcard_base
    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.mosi_io_num = GPIO_NUM_6;
    bus.miso_io_num = GPIO_NUM_8;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 2;
    sdmmc_card_t* card = nullptr;
    const auto result = esp_vfs_fat_sdspi_mount(mountPoint, &host, &slot, &options, &card);
    if (result != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        std::printf("microSD mount failed: %s\n", esp_err_to_name(result));
        return false;
    }
    std::puts("microSD mounted");
    return true;
}

uint32_t littleEndian(const uint8_t* data, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) {
        value |= static_cast<uint32_t>(data[i]) << (8 * i);
    }
    return value;
}

struct Wave {
    uint32_t rate = 0;
    unsigned channels = 0;
    unsigned bits = 0;
    uint32_t bytes = 0;
};

bool readWave(FILE* file, Wave& wave)
{
    uint8_t riff[12];
    if (std::fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
        std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }
    const uint64_t end = 8ull + littleEndian(riff + 4, 4);
    for (uint64_t offset = sizeof(riff); offset + 8 <= end;) {
        uint8_t header[8];
        if (std::fread(header, 1, sizeof(header), file) != sizeof(header)) {
            return false;
        }
        const uint32_t size = littleEndian(header + 4, 4);
        const uint64_t next = offset + sizeof(header) + size + (size & 1);
        if (next > end || next > INT32_MAX) {
            return false;
        }
        if (std::memcmp(header, "fmt ", 4) == 0) {
            uint8_t format[16];
            if (size < sizeof(format) ||
                std::fread(format, 1, sizeof(format), file) != sizeof(format) ||
                littleEndian(format, 2) != 1) {
                return false;
            }
            wave.channels = littleEndian(format + 2, 2);
            wave.rate = littleEndian(format + 4, 4);
            wave.bits = littleEndian(format + 14, 2);
            if ((wave.channels != 1 && wave.channels != 2) ||
                (wave.bits != 8 && wave.bits != 16) || wave.rate == 0 ||
                littleEndian(format + 12, 2) != wave.channels * wave.bits / 8) {
                return false;
            }
        } else if (std::memcmp(header, "data", 4) == 0) {
            wave.bytes = size;
            return wave.rate && size && size % (wave.channels * wave.bits / 8) == 0;
        }
        if (std::fseek(file, static_cast<long>(next), SEEK_SET) != 0) {
            return false;
        }
        offset = next;
    }
    return false;
}

bool playFile(const std::string& path)
{
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
    Wave wave;
    if (!file || !readWave(file.get(), wave)) {
        return false;
    }
    std::printf("Playing %s: %lu Hz, %u-bit, %u channel(s), %lu bytes\n",
                path.c_str(), static_cast<unsigned long>(wave.rate), wave.bits,
                wave.channels, static_cast<unsigned long>(wave.bytes));
    uint32_t remaining = wave.bytes;
    size_t index = 0;
    while (remaining) {
        auto& buffer = buffers[index];
        const auto size = std::min<size_t>(remaining, sizeof(buffer));
        if (std::fread(buffer.data(), 1, size, file.get()) != size) {
            break;
        }
        while (M5.Speaker.isPlaying(channel) >= 2) {
            vTaskDelay(1);
        }
        const bool stereo = wave.channels == 2;
        const bool queued = wave.bits == 16
            ? M5.Speaker.playRaw(buffer.data(), size / 2, wave.rate, stereo, 1, channel)
            : M5.Speaker.playRaw(reinterpret_cast<const uint8_t*>(buffer.data()), size, wave.rate, stereo, 1, channel);
        if (!queued) {
            break;
        }
        index = (index + 1) % buffers.size();
        remaining -= size;
    }
    while (M5.Speaker.isPlaying(channel)) {
        vTaskDelay(1);
    }
    return remaining == 0;
}

void playDirectory(DIR* dir)
{
    unsigned played = 0;
    while (const auto* entry = readdir(dir)) {
        const char* extension = std::strrchr(entry->d_name, '.');
        if (entry->d_type == DT_DIR || !extension || strcasecmp(extension, ".wav") != 0) {
            continue;
        }
        const auto path = std::string(mountPoint) + "/" + entry->d_name;
        if (playFile(path)) {
            std::printf("Finished %s\n", path.c_str());
            ++played;
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            std::printf("Could not play %s\n", path.c_str());
        }
    }
    if (!played) {
        std::puts("No playable WAV files");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Mic.end();
    if (!M5.Speaker.begin()) {
        std::puts("Speaker unavailable");
        return;
    }
    M5.Speaker.setVolume(64);
    std::puts("VoiceS3R SD WAV playback: Atomic TFCard Base A135");
    while (!mountCard()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    for (;;) {
        std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(mountPoint), closedir);
        if (dir) {
            playDirectory(dir.get());
        } else {
            std::puts("Cannot open microSD root");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
