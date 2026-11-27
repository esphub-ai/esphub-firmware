// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>
#include <utility>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace {
constexpr char mountPoint[] = "/sd";
constexpr char path[] = "/sd/11-fanfare.mp3";
constexpr unsigned dmaFrames = 256;

void show(const char* state)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Atomic SPK MP3", 4, 8);
    M5.Display.drawString(state, 4, 32);
    std::puts(state);
}

unsigned tagSize(const uint8_t* p, unsigned shift)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 4; ++i) n = (n << shift) | (p[i] & ((1u << shift) - 1));
    return n;
}

void readTags(FILE* file)
{
    uint8_t header[10];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
        std::memcmp(header, "ID3", 3)) {
        std::rewind(file);
        return;
    }
    const auto end = 10 + tagSize(header + 6, 7);
    const unsigned version = header[3], flags = header[5];
    if ((version == 3 || version == 4) && !(flags & 0xc0)) {
        for (unsigned offset = 10; offset + 10 <= end;) {
            uint8_t frame[10];
            if (std::fread(frame, 1, sizeof(frame), file) != sizeof(frame) || !frame[0]) break;
            const auto size = tagSize(frame + 4, version == 4 ? 7 : 8);
            if (size > end - offset - 10) break;
            if (frame[0] == 'T' && !frame[9] && size > 1) {
                char text[64]{};
                const auto encoding = std::fgetc(file);
                if (encoding == 0 || encoding == 3) {
                    std::fread(text, 1, std::min<unsigned>(size - 1, sizeof(text) - 1), file);
                } else if (encoding == 1 || encoding == 2) {
                    bool little = false;
                    unsigned index = 0;
                    for (unsigned left = size - 1; left >= 2 && index < sizeof(text) - 1; left -= 2) {
                        const int a = std::fgetc(file), b = std::fgetc(file);
                        if (a == EOF || b == EOF || (!a && !b)) break;
                        if (a == 0xff && b == 0xfe) { little = true; continue; }
                        if (a == 0xfe && b == 0xff) { little = false; continue; }
                        const unsigned code = little ? a | (b << 8) : (a << 8) | b;
                        text[index++] = code < 128 ? static_cast<char>(code) : '?';
                    }
                }
                std::printf("ID3 callback for: %.4s = '%s'\n", reinterpret_cast<char*>(frame), text);
            }
            offset += 10 + size;
            if (std::fseek(file, offset, SEEK_SET)) break;
        }
    }
    std::fseek(file, end + ((version == 4 && (flags & 0x10)) ? 10 : 0), SEEK_SET);
}

struct Playback {
    uint8_t input[16384];
    int16_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t stereo[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_t decoder{};
    i2s_chan_handle_t output{};
    unsigned rate{};
    bool enabled{};

    ~Playback()
    {
        if (enabled) i2s_channel_disable(output);
        if (output) i2s_del_channel(output);
    }

    esp_err_t configure(unsigned hz)
    {
        if (!output) {
            i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
            channel.dma_desc_num = 4;
            channel.dma_frame_num = dmaFrames;
            channel.auto_clear = true;
            auto result = i2s_new_channel(&channel, &output, nullptr);
            if (result != ESP_OK) return result;
            i2s_std_config_t config{};
            config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz);
            config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
            config.gpio_cfg = {GPIO_NUM_NC, GPIO_NUM_5, GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_NC, {}};
            result = i2s_channel_init_std_mode(output, &config);
            if (result != ESP_OK) return result;
        } else {
            auto result = i2s_channel_disable(output);
            if (result != ESP_OK) return result;
            enabled = false;
            const i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(hz);
            result = i2s_channel_reconfig_std_clock(output, &clock);
            if (result != ESP_OK) return result;
        }
        const auto result = i2s_channel_enable(output);
        enabled = result == ESP_OK;
        if (enabled) rate = hz;
        return result;
    }

    bool write(const int16_t* pcm, size_t frames)
    {
        size_t written = 0;
        const size_t bytes = frames * 2 * sizeof(int16_t);
        const auto result = i2s_channel_write(output, pcm, bytes, &written, 1000);
        if (result == ESP_OK && written == bytes) return true;
        std::printf("I2S write %s (%u/%u bytes)\n", esp_err_to_name(result), unsigned(written), unsigned(bytes));
        return false;
    }

    bool drain()
    {
        const int16_t silence[dmaFrames * 2]{};
        // Fill one ring plus a descriptor so that queued audio has left the DMA ring.
        for (unsigned i = 0; i < 5; ++i) if (!write(silence, dmaFrames)) return false;
        return true;
    }

    bool play(FILE* file)
    {
        mp3dec_init(&decoder);
        size_t available = 0, total = 0;
        for (;;) {
            available += std::fread(input + available, 1, sizeof(input) - available, file);
            if (std::ferror(file)) return false;
            if (!available) break;
            mp3dec_frame_info_t info{};
            const int frames = mp3dec_decode_frame(&decoder, input, available, decoded, &info);
            if (!info.frame_bytes) break;
            available -= info.frame_bytes;
            std::memmove(input, input + info.frame_bytes, available);
            if (!frames) continue;
            if (info.hz != rate) {
                if (enabled && !drain()) return false;
                const auto result = configure(info.hz);
                std::printf("STATUS(mp3) %s, %d Hz, %d channel(s)\n", esp_err_to_name(result), info.hz, info.channels);
                if (result != ESP_OK) return false;
                show("Playing MP3");
            }
            const int16_t* pcm = decoded;
            if (info.channels == 1) {
                for (int i = 0; i < frames; ++i) stereo[2 * i] = stereo[2 * i + 1] = decoded[i];
                pcm = stereo;
            }
            if (!write(pcm, frames)) return false;
            total += frames;
        }
        std::printf("STATUS(mp3) decoded %u PCM frames\n", unsigned(total));
        return total && drain();
    }
};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto board = M5.config();
    board.internal_imu = false;
    board.internal_rtc = false;
    M5.begin(board);
    // The AtomS3 internal I2C bus shares GPIO38/39 with the SPK Base I2S output.
    M5.In_I2C.release();
    show("Init...");
    vTaskDelay(pdMS_TO_TICKS(400));

    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.miso_io_num = GPIO_NUM_8;
    bus.mosi_io_num = GPIO_NUM_6;
    bus.quadwp_io_num = bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    const auto spi = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
    if (spi != ESP_OK) {
        show("Card Mount Failed");
        std::puts(esp_err_to_name(spi));
        return;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SDSPI_SLOT_NO_CS; // The Atomic SPK Base holds CS low.
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 1;
    sdmmc_card_t* card = nullptr;
    const auto mounted = esp_vfs_fat_sdspi_mount(mountPoint, &host, &device, &options, &card);
    if (mounted != ESP_OK) {
        show("Card Mount Failed");
        std::puts(esp_err_to_name(mounted));
    } else {
        if (FILE* file = std::fopen(path, "rb")) {
            std::printf("Opened %s\n", path);
            readTags(file);
            // minimp3's decoder scratch needs more stack than the IDF main task.
            std::pair<FILE*, TaskHandle_t> job{file, xTaskGetCurrentTaskHandle()};
            const auto started = xTaskCreate([](void* arg) {
                std::unique_ptr<Playback> playback(new (std::nothrow) Playback{});
                const auto* job = static_cast<const std::pair<FILE*, TaskHandle_t>*>(arg);
                FILE* input = job->first;
                const bool done = playback && playback->play(input);
                playback.reset();
                std::fclose(input);
                show(done ? "MP3 done" : "MP3 decode failed");
                xTaskNotifyGive(job->second);
                vTaskDelete(nullptr);
            }, "mp3", 24576, &job, 5, nullptr);
            if (started == pdPASS) {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            } else {
                std::fclose(file);
                show("MP3 decode failed");
            }
        } else {
            show("MP3 file missing");
            std::printf("Could not open %s\n", path);
        }
        esp_vfs_fat_sdcard_unmount(mountPoint, card);
    }
    spi_bus_free(SPI2_HOST);
}
