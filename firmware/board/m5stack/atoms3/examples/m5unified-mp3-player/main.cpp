// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "AudioUI.h"
#include "audio_file.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <dirent.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

namespace {
constexpr const char* mountPath = "/sd";
constexpr const char* mediaPath = "/sd/mp3";
constexpr size_t maxTracks = 256;
AudioUI ui;
std::atomic<int> trackRequest{0};
std::atomic<bool> controlsEnabled{false};

bool hasExtension(const char* name, const char* ext)
{
    const auto n = std::strlen(name);
    return n >= 5 && strcasecmp(name + n - 4, ext) == 0;
}

std::vector<std::string> scan()
{
    std::vector<std::string> tracks;
    auto* dir = opendir(mediaPath);
    if (!dir) return tracks;
    while (const auto* entry = readdir(dir)) {
        if (!hasExtension(entry->d_name, ".wav") && !hasExtension(entry->d_name, ".mp3")) continue;
        auto path = std::string(mediaPath) + "/" + entry->d_name;
        struct stat info{};
        if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)) tracks.push_back(std::move(path));
        if (tracks.size() == maxTracks) {
            std::puts("Playlist limit: 256 files");
            break;
        }
    }
    closedir(dir);
    std::sort(tracks.begin(), tracks.end());
    return tracks;
}

struct Player {
    struct Buffer {
        std::array<int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm{};
        std::atomic<bool> busy{false};
    };
    std::array<Buffer, 2> output{};
    alignas(int16_t) std::array<uint8_t, 8192> input{};
    std::array<int16_t, 1024> monitor{};
    mp3dec_t decoder{};
    TaskHandle_t owner = nullptr;
    unsigned slot = 0;
    unsigned submitted = 0;
    std::atomic<unsigned> returned{0};

    static void release(void* arg, const void* data, uint8_t)
    {
        auto& self = *static_cast<Player*>(arg);
        for (auto& buffer : self.output) {
            if (data != buffer.pcm.data()) continue;
            self.returned.fetch_add(1, std::memory_order_relaxed);
            buffer.busy.store(false, std::memory_order_release);
            xTaskNotifyGive(self.owner);
            break;
        }
    }

    bool waitSlot()
    {
        const auto deadline = M5.millis() + 1500;
        while (output[slot].busy.load(std::memory_order_acquire)) {
            if (trackRequest.load() || int32_t(deadline - M5.millis()) <= 0) return false;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        }
        return !trackRequest.load();
    }

    bool queue(unsigned frames, unsigned hz, unsigned channels)
    {
        auto& buffer = output[slot];
        const auto count = std::min<size_t>(frames, monitor.size() / 2);
        for (size_t i = 0; i < count; ++i) {
            monitor[2 * i] = buffer.pcm[i * channels];
            monitor[2 * i + 1] = buffer.pcm[i * channels + channels - 1];
        }
        ui.feed(monitor.data(), count);
        buffer.busy.store(true, std::memory_order_release);
        if (!M5.Speaker.playRaw(buffer.pcm.data(), frames * channels, hz, channels == 2, 1, 0)) {
            buffer.busy.store(false, std::memory_order_release);
            return false;
        }
        ++submitted;
        slot ^= 1;
        return true;
    }

    bool mp3(FILE* file)
    {
        const auto tags = readTags(file);
        ui.setMeta(1, tags.title, "Title");
        ui.setMeta(2, tags.performer, "Performer");
        mp3dec_init(&decoder);
        size_t available = 0;
        unsigned total = 0;
        bool eof = false;
        while (waitSlot()) {
            if (!eof) {
                const auto read = std::fread(input.data() + available, 1, input.size() - available, file);
                if (std::ferror(file)) return false;
                available += read;
                eof = std::feof(file);
            }
            if (!available) break;
            mp3dec_frame_info_t info{};
            const int frames = mp3dec_decode_frame(&decoder, input.data(), available, output[slot].pcm.data(), &info);
            if (info.frame_bytes <= 0) break;
            available -= info.frame_bytes;
            std::memmove(input.data(), input.data() + info.frame_bytes, available);
            if (!frames) continue;
            if (!total) std::printf("Format MP3 rate=%d channels=%d\n", info.hz, info.channels);
            if (!queue(frames, info.hz, info.channels)) return false;
            total += frames;
        }
        std::printf("Decoded MP3 frames=%u\n", total);
        return total > 0;
    }

    bool wav(FILE* file)
    {
        Wav header;
        if (!parseWav(file, header)) return false;
        std::printf("Format WAV rate=%u bits=%u channels=%u\n",
                    unsigned(header.rate), unsigned(header.bits), unsigned(header.channels));
        for (auto remaining = header.bytes; remaining && !trackRequest.load();) {
            if (!waitSlot()) return false;
            const auto capacity = output[slot].pcm.size() / header.channels * header.align;
            const auto size = std::min({size_t(remaining), size_t(4096), capacity});
            if (std::fread(input.data(), 1, size, file) != size) return false;
            const auto samples = size / (header.bits / 8);
            for (size_t i = 0; i < samples; ++i) {
                output[slot].pcm[i] = header.bits == 8
                    ? int16_t((int(input[i]) - 128) * 256)
                    : int16_t(le16(input.data() + 2 * i));
            }
            if (!queue(samples / header.channels, header.rate, header.channels)) return false;
            remaining -= size;
        }
        return submitted > 0;
    }

    bool play(const std::string& path)
    {
        auto* file = std::fopen(path.c_str(), "rb");
        if (!file) return false;
        slot = submitted = 0;
        returned.store(0);
        bool ok = hasExtension(path.c_str(), ".mp3") ? mp3(file) : wav(file);
        std::fclose(file);
        // Preserve the last source sample through the mixer's word alignment.
        static constexpr int16_t pad[4]{};
        if (ok && !trackRequest.load())
            ok = M5.Speaker.playRaw(pad, std::size(pad), 48000, true, 1, 0);
        const auto deadline = M5.millis() + 1800;
        while (ok && !trackRequest.load() && M5.Speaker.isPlaying(0)) {
            if (int32_t(deadline - M5.millis()) <= 0) { ok = false; break; }
            vTaskDelay(1);
        }
        if (ok && !trackRequest.load()) M5.delay(65);
        // Join the mixer before the decoder can reuse any of its buffers.
        M5.Speaker.end();
        const auto count = returned.load();
        std::printf("Buffers submitted=%u released=%u\n", submitted, count);
        for (auto& buffer : output) buffer.busy.store(false);
        ui.feed(nullptr, 0);
        return ok && count == submitted;
    }
};
Player player;

void decodeTask(void*)
{
    player.owner = xTaskGetCurrentTaskHandle();
    M5.Speaker.setBufferReleaseCallback(&player, Player::release);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SDSPI_SLOT_NO_CS;
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 3;

    for (;;) {
        sdmmc_card_t* card = nullptr;
        if (esp_vfs_fat_sdspi_mount(mountPath, &host, &device, &options, &card) != ESP_OK) {
            ui.setMeta(1, "no SD card");
            M5.delay(500);
            continue;
        }
        ui.clearMeta();
        auto tracks = scan();
        std::printf("SD mounted tracks=%u internal_free=%u decode_stack=%u\n",
                    unsigned(tracks.size()),
                    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                    unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        if (tracks.empty()) {
            ui.setMeta(1, "no *.mp3 / *.wav");
            ui.setMeta(2, "in /mp3");
            std::puts("no *.mp3 / *.wav in /mp3");
        } else {
            size_t index = 0;
            size_t failures = 0;
            controlsEnabled.store(true);
            while (failures < tracks.size()) {
                ui.clearMeta();
                const auto& path = tracks[index];
                ui.setMeta(0, std::strrchr(path.c_str(), '/') + 1, "file");
                std::printf("Track %s\n", path.c_str());
                const bool played = player.play(path);
                const int request = trackRequest.exchange(0);
                if (request) {
                    std::printf("Track request=%d\n", request);
                } else if (!played) {
                    ui.setMeta(1, "(cannot play)");
                    std::printf("Skip %s (cannot play)\n", path.c_str());
                    ++failures;
                    M5.delay(300);
                } else {
                    failures = 0;
                    std::printf("Track complete %s free=%u stack=%u\n", path.c_str(),
                                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                                unsigned(uxTaskGetStackHighWaterMark(nullptr)));
                }
                const int count = int(tracks.size());
                index = (int(index) + (request ? request : 1) % count + count) % count;
            }
            controlsEnabled.store(false);
        }
        esp_vfs_fat_sdcard_unmount(mountPath, card);
        M5.delay(500);
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_speaker.atomic_spk = true;
    config.external_display_value = 0;
    config.internal_imu = config.internal_rtc = false;
    M5.begin(config);
    M5.In_I2C.release();
    M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);
    ui.setup(&M5.Display, "MP3 player");
    auto speaker = M5.Speaker.config();
    if (speaker.pin_bck != 5 || speaker.pin_ws != 39 || speaker.pin_data_out != 38) {
        ui.setMeta(1, "Atomic SPK missing");
        ui.loop();
        return;
    }
    speaker.i2s_port = I2S_NUM_0;
    speaker.stereo = true;
    speaker.sample_rate = 48000;
    speaker.magnification = 1;
    M5.Speaker.config(speaker);
    M5.Speaker.setVolume(160);

    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.miso_io_num = GPIO_NUM_8;
    bus.mosi_io_num = GPIO_NUM_6;
    bus.quadwp_io_num = bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK ||
        xTaskCreatePinnedToCore(decodeTask, "decodeTask", 24576, nullptr, 2, nullptr, 1) != pdPASS) {
        ui.setMeta(1, "Player init failed");
        ui.loop();
        return;
    }
    std::puts("MP3 player ready");
    for (;;) {
        M5.update();
        if (controlsEnabled.load()) {
            const auto input = ui.update();
            if (input.track) trackRequest.fetch_add(input.track);
            if (input.volume && M5.BtnA.wasHold())
                std::printf("Volume %u\n", unsigned(M5.Speaker.getVolume()));
        }
        ui.loop();
        M5.delay(20);
    }
}
