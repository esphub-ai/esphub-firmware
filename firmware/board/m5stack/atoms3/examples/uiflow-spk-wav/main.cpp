// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace {
constexpr unsigned sampleRate = 48000;
constexpr unsigned dmaFrames = 256;
constexpr unsigned dmaBuffers = 8;
constexpr int volume = 80;
constexpr int magnification = 16;
constexpr char flashPath[] = "/flash/res/audio/66.wav";
constexpr char sdPath[] = "/sd/66.wav";

constexpr auto flashWav()
{
    // Original one-second 600 Hz triangle tone, with 8-bit unsigned PCM samples.
    std::array<uint8_t, 44 + sampleRate> wav{
        'R', 'I', 'F', 'F', 0xa4, 0xbb, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x80, 0xbb, 0, 0, 0x80, 0xbb, 0, 0, 1, 0, 8, 0,
        'd', 'a', 't', 'a', 0x80, 0xbb, 0, 0
    };
    for (unsigned i = 0; i < sampleRate; ++i) {
        const int phase = i % 80;
        wav[44 + i] = 128 + (phase < 40 ? phase - 20 : 60 - phase) * 3;
    }
    return wav;
}

// The Flash WAV is a read-only resource within this OTA image.
constexpr auto flashAudio = flashWav();

struct Input {
    const uint8_t* flash;
    FILE* file;
    size_t size;
    size_t position = 0;

    bool read(void* buffer, size_t count)
    {
        if (count > size - position) return false;
        if (file) {
            if (std::fread(buffer, 1, count, file) != count) return false;
        } else {
            std::memcpy(buffer, flash + position, count);
        }
        position += count;
        return true;
    }

    bool seek(size_t offset)
    {
        if (offset > size || (file && std::fseek(file, offset, SEEK_SET) != 0)) return false;
        position = offset;
        return true;
    }
};

uint32_t le32(const uint8_t* bytes)
{
    return bytes[0] | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

unsigned le16(const uint8_t* bytes)
{
    return bytes[0] | unsigned(bytes[1]) << 8;
}

struct Wav {
    unsigned rate = 0;
    unsigned channels = 0;
    unsigned bits = 0;
    unsigned frames = 0;
};

bool parseWav(Input& input, Wav& wav)
{
    uint8_t riff[12];
    if (!input.read(riff, sizeof(riff)) || std::memcmp(riff, "RIFF", 4) ||
        std::memcmp(riff + 8, "WAVE", 4)) return false;
    const auto riffSize = le32(riff + 4);
    if (riffSize < 4 || riffSize > input.size - 8) return false;
    const size_t end = 8 + riffSize;
    size_t dataOffset = 0;
    size_t dataSize = 0;
    unsigned blockSize = 0;

    while (end - input.position >= 8) {
        uint8_t chunk[8];
        if (!input.read(chunk, sizeof(chunk))) return false;
        const auto size = le32(chunk + 4);
        const auto remaining = end - input.position;
        if (size > remaining || size % 2 > remaining - size) return false;
        const size_t next = input.position + size + size % 2;
        if (!std::memcmp(chunk, "fmt ", 4) && !blockSize) {
            uint8_t format[16];
            if (size < sizeof(format) || !input.read(format, sizeof(format)) ||
                le16(format) != 1) return false;
            wav.channels = le16(format + 2);
            wav.rate = le32(format + 4);
            wav.bits = le16(format + 14);
            if ((wav.channels != 1 && wav.channels != 2) || !wav.rate || wav.rate > 192000 ||
                (wav.bits != 8 && wav.bits != 16)) return false;
            blockSize = wav.channels * wav.bits / 8;
            if (le16(format + 12) != blockSize || le32(format + 8) != wav.rate * blockSize) return false;
        } else if (!std::memcmp(chunk, "data", 4) && !dataOffset) {
            dataOffset = input.position;
            dataSize = size;
        }
        if (blockSize && dataOffset) {
            if (!dataSize || dataSize % blockSize || !input.seek(dataOffset)) return false;
            wav.frames = dataSize / blockSize;
            return true;
        }
        if (!input.seek(next)) return false;
    }
    return false;
}

bool readFrame(Input& input, const Wav& wav, int& sample)
{
    const unsigned bytes = wav.bits / 8;
    uint8_t pcm[4];
    if (!input.read(pcm, bytes * wav.channels)) return false;
    const auto decode = [bytes](const uint8_t* p) {
        if (bytes == 1) return (int(p[0]) - 128) * 256;
        const unsigned value = le16(p);
        return value < 32768 ? int(value) : int(value) - 65536;
    };
    sample = decode(pcm);
    if (wav.channels == 2) sample = (sample + decode(pcm + bytes)) / 2;
    return true;
}

esp_err_t pushAudio(i2s_chan_handle_t output, Input& input, const Wav& wav)
{
    int current;
    int next;
    if (!readFrame(input, wav, current)) return ESP_FAIL;
    next = current;
    if (wav.frames > 1 && !readFrame(input, wav, next)) return ESP_FAIL;
    unsigned phase = 0;
    unsigned sourceFrame = 0;
    size_t buffered = 0;
    int16_t pcm[dmaFrames]{};

    while (sourceFrame < wav.frames) {
        // Match the 48 kHz SpeakerBase output clock using linear interpolation.
        const int sample = current + (next - current) * int64_t(phase) / sampleRate;
        // M5Unified mono mixes two channels and shifts the accumulator by eight.
        const int64_t scaled = int64_t(sample) * 2 * magnification * volume * volume * 255 * 255;
        pcm[buffered++] = std::clamp<int64_t>(scaled / (1LL << 36), -32768, 32767);
        phase += wav.rate;
        while (phase >= sampleRate && ++sourceFrame < wav.frames) {
            phase -= sampleRate;
            current = next;
            if (sourceFrame + 1 == wav.frames) next = current;
            else if (!readFrame(input, wav, next)) return ESP_FAIL;
        }
        if (buffered == dmaFrames || sourceFrame == wav.frames) {
            const size_t bytes = buffered * sizeof(pcm[0]);
            size_t written = 0;
            const auto result = i2s_channel_write(output, pcm, bytes, &written, 1000);
            if (result != ESP_OK) return result;
            if (written != bytes) return ESP_FAIL;
            buffered = 0;
        }
    }
    // Keep writing silence until the hardware has consumed the last audio buffer.
    std::fill(std::begin(pcm), std::end(pcm), 0);
    for (unsigned i = 0; i <= dmaBuffers; ++i) {
        size_t written = 0;
        const auto result = i2s_channel_write(output, pcm, sizeof(pcm), &written, 1000);
        if (result != ESP_OK) return result;
        if (written != sizeof(pcm)) return ESP_FAIL;
    }
    return ESP_OK;
}

void show(const char* state, bool failed = false)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.drawString("Atomic SPK WAV", 4, 8);
    M5.Display.setTextColor(failed ? TFT_RED : TFT_GREEN);
    M5.Display.drawString(state, 4, 32);
    std::puts(state);
}

bool play(i2s_chan_handle_t output, Input& input, const char* path, const char* playing)
{
    Wav wav;
    if (!parseWav(input, wav)) {
        std::printf("Invalid PCM WAV: %s\n", path);
        return false;
    }
    show(playing);
    std::printf("Playing %s: %u Hz, %u-bit, %u channel(s), %u frames\n",
                path, wav.rate, wav.bits, wav.channels, wav.frames);
    const auto enabled = i2s_channel_enable(output);
    if (enabled != ESP_OK) {
        std::puts(esp_err_to_name(enabled));
        return false;
    }
    const auto played = pushAudio(output, input, wav);
    const auto stopped = i2s_channel_disable(output);
    if (played != ESP_OK || stopped != ESP_OK) {
        std::puts(esp_err_to_name(played == ESP_OK ? stopped : played));
        return false;
    }
    std::printf("Playback complete: %s\n", path);
    return true;
}

void playCard(i2s_chan_handle_t output)
{
    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.miso_io_num = GPIO_NUM_8;
    bus.mosi_io_num = GPIO_NUM_6;
    bus.quadwp_io_num = bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    const auto spi = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
    if (spi != ESP_OK) {
        show("SD SPI init failed", true);
        std::puts(esp_err_to_name(spi));
        return;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SDSPI_SLOT_NO_CS; // The Atomic SPK Base holds CS low.
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 1;
    sdmmc_card_t* card = nullptr;
    const auto mounted = esp_vfs_fat_sdspi_mount("/sd", &host, &device, &options, &card);
    if (mounted != ESP_OK) {
        show("Card Mount Failed", true);
        std::puts(esp_err_to_name(mounted));
    } else {
        if (FILE* file = std::fopen(sdPath, "rb")) {
            const long size = std::fseek(file, 0, SEEK_END) == 0 ? std::ftell(file) : -1;
            bool played = false;
            if (size >= 0 && std::fseek(file, 0, SEEK_SET) == 0) {
                Input input{nullptr, file, size_t(size)};
                played = play(output, input, sdPath, "Playing SD WAV");
            }
            std::fclose(file);
            show(played ? "SD WAV done" : "SD WAV failed", !played);
        } else {
            show("SD WAV file missing", true);
            std::printf("Could not open %s\n", sdPath);
        }
        esp_vfs_fat_sdcard_unmount("/sd", card);
    }
    spi_bus_free(SPI2_HOST);
    std::puts("SD resources released");
}

void playFiles()
{
    i2s_chan_handle_t output = nullptr;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.dma_desc_num = dmaBuffers;
    channel.dma_frame_num = dmaFrames;
    channel.auto_clear = true;
    auto result = i2s_new_channel(&channel, &output, nullptr);
    if (result == ESP_OK) {
        i2s_std_config_t config{};
        config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
        config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        config.gpio_cfg = {GPIO_NUM_NC, GPIO_NUM_5, GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_NC, {}};
        result = i2s_channel_init_std_mode(output, &config);
    }
    if (result == ESP_OK) {
        std::printf("I2S1 BCLK=5 WS=39 DOUT=38, %u Hz mono, volume %d/255, DMA %u x %u\n",
                    sampleRate, volume, dmaFrames, dmaBuffers);
        Input input{flashAudio.data(), nullptr, flashAudio.size()};
        if (play(output, input, flashPath, "Playing Flash WAV")) {
            show("Flash WAV done");
            playCard(output);
        } else {
            show("Flash WAV failed", true);
        }
    } else {
        show("I2S init failed", true);
        std::puts(esp_err_to_name(result));
    }
    if (output) i2s_del_channel(output);
    std::puts("I2S resources released");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto board = M5.config();
    board.internal_imu = false;
    board.internal_rtc = false;
    M5.begin(board);
    // GPIO38/39 are shared by the AtomS3's internal I2C bus and SPK Base I2S.
    M5.In_I2C.release();
    show("Init...");
    vTaskDelay(pdMS_TO_TICKS(400));
    playFiles();
    for (;;) {
        M5.update();
        vTaskDelay(1);
    }
}
