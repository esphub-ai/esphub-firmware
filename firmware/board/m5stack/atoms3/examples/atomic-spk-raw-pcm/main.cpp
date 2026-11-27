// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2s_std.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr unsigned sampleRate = 44100;
constexpr unsigned sampleCount = 46000;
constexpr int volume = 64;
constexpr unsigned dmaFrames = 256;
constexpr unsigned dmaDescriptors = 4;

void show(const char* state)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Atomic SPK PCM", 4, 8);
    M5.Display.drawString(state, 4, 32);
    std::puts(state);
}

uint8_t clickSample(unsigned position)
{
    // An original triangle-wave click with a linear decay, followed by silence.
    constexpr unsigned decay = 4096;
    if (position >= decay) return 128;
    const unsigned phase = position % 40;
    const int triangle = phase < 20 ? int(phase) - 10 : 30 - int(phase);
    return 128 + triangle * 11 * int(decay - position) / int(decay);
}

int16_t outputSample(uint8_t sample)
{
    // M5Unified applies the square of its 0–255 master-volume setting.
    const int centered = int(sample) - 128;
    return centered * 256 * volume * volume / (255 * 255);
}

esp_err_t play(i2s_chan_handle_t speaker)
{
    auto result = i2s_channel_enable(speaker);
    if (result != ESP_OK) return result;

    int16_t pcm[dmaFrames * 2];
    // Send a full ring of silence after the file to drain the queued audio.
    constexpr unsigned frames = sampleCount + dmaFrames * (dmaDescriptors + 1);
    for (unsigned start = 0; start < frames; start += dmaFrames) {
        const unsigned count = frames - start < dmaFrames ? frames - start : dmaFrames;
        for (unsigned i = 0; i < count; ++i) {
            const auto sample = outputSample(clickSample(start + i));
            pcm[i * 2] = pcm[i * 2 + 1] = sample;
        }
        size_t written = 0;
        const size_t bytes = count * 2 * sizeof(int16_t);
        result = i2s_channel_write(speaker, pcm, bytes, &written, 1000);
        if (result != ESP_OK) break;
        if (written != bytes) {
            result = ESP_FAIL;
            break;
        }
    }
    const auto stopped = i2s_channel_disable(speaker);
    return result == ESP_OK ? stopped : result;
}
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

    i2s_chan_handle_t speaker = nullptr;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = dmaDescriptors;
    channel.dma_frame_num = dmaFrames;
    channel.auto_clear = true;
    auto result = i2s_new_channel(&channel, &speaker, nullptr);
    if (result == ESP_OK) {
        i2s_std_config_t config{};
        config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
        config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        config.gpio_cfg = {GPIO_NUM_NC, GPIO_NUM_5, GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_NC, {}};
        result = i2s_channel_init_std_mode(speaker, &config);
    }
    if (result != ESP_OK) {
        if (speaker) i2s_del_channel(speaker);
        show("I2S init failed");
        std::puts(esp_err_to_name(result));
        return;
    }

    std::printf("I2S0 BCLK=5 WS=39 DOUT=38, volume %d/255\n", volume);
    show("Click BtnA");
    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            show("Playing PCM");
            std::printf("BtnA: %u frames at %u Hz, unsigned 8-bit mono\n", sampleCount, sampleRate);
            result = play(speaker);
            show(result == ESP_OK ? "PCM done" : "PCM failed");
            if (result != ESP_OK) std::puts(esp_err_to_name(result));
            M5.Display.drawString("Click BtnA", 4, 52);
        }
        vTaskDelay(1);
    }
}
