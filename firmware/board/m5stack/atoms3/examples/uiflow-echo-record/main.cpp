// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2s_std.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
constexpr uint8_t codec = 0x18;
constexpr uint8_t amp = 0x43;
constexpr uint8_t ampOutput = 0x05;
constexpr uint32_t rate = 16000;
constexpr unsigned dmaFrames = 250;
constexpr unsigned dmaCount = 6;
constexpr unsigned blockSize = dmaFrames * 2 * sizeof(int16_t);
std::array<uint8_t, rate / 2 * 2 * sizeof(int16_t)> pcm;
constexpr std::array<uint8_t, blockSize> silence{};

bool reg(uint8_t device, uint8_t address, uint8_t value)
{
    if (M5.In_I2C.writeRegister8(device, address, value, 100000)) return true;
    std::printf("Echo I2C write failed device=%02x reg=%02x\n", device, address);
    return false;
}

bool initCodec()
{
    if (!M5.In_I2C.scanID(codec, 100000) || !M5.In_I2C.scanID(amp, 100000)) {
        std::puts("Echo codec or amplifier missing");
        return false;
    }
    if (!reg(codec, 0x00, 0x1f)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    // 16 kHz with codec MCLK derived from the 1.024 MHz BCLK.
    // Two 32-bit bus slots carry 16-bit PCM in the ESP32 DMA buffers.
    constexpr uint8_t codecSetup[][2] = {
        {0x00, 0x00}, {0x00, 0x80}, {0x01, 0xbf}, {0x02, 0x10},
        {0x03, 0x10}, {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03},
        {0x07, 0x00}, {0x08, 0xff}, {0x09, 0x10}, {0x0a, 0x10},
        {0x0d, 0x01}, {0x0e, 0x02}, {0x12, 0x00}, {0x13, 0x10},
        {0x1c, 0x6a}, {0x37, 0x08}, {0x32, 203}, {0x14, 0x1a},
    };
    for (const auto& setting : codecSetup) {
        if (!reg(codec, setting[0], setting[1])) return false;
    }
    return reg(amp, 0x07, 0x00) && reg(amp, 0x0d, 0xff) &&
           reg(amp, 0x03, 0x6e) && reg(amp, ampOutput, 0xff);
}

esp_err_t initI2s(i2s_chan_handle_t& channel, bool recording)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cfg.dma_frame_num = dmaFrames;
    cfg.dma_desc_num = dmaCount;
    cfg.auto_clear = true;
    esp_err_t result = i2s_new_channel(&cfg, recording ? nullptr : &channel,
                                     recording ? &channel : nullptr);
    if (result != ESP_OK) return result;
    i2s_std_config_t bus{};
    bus.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    bus.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    bus.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    bus.gpio_cfg = {GPIO_NUM_NC, GPIO_NUM_8, GPIO_NUM_6,
                    recording ? GPIO_NUM_NC : GPIO_NUM_5,
                    recording ? GPIO_NUM_7 : GPIO_NUM_NC, {}};
    return i2s_channel_init_std_mode(channel, &bus);
}

bool capture()
{
    i2s_chan_handle_t rx = nullptr;
    size_t captured = 0;
    esp_err_t result = initI2s(rx, true);
    if (result == ESP_OK) result = i2s_channel_enable(rx);
    if (result == ESP_OK) {
        std::puts("Echo I2S1 RX start 16000 Hz stereo 16-bit");
        result = i2s_channel_read(rx, pcm.data(), pcm.size(), &captured, 2000);
        i2s_channel_disable(rx);
    }
    if (rx) i2s_del_channel(rx);
    std::printf("Echo capture %u bytes (%s)\n", unsigned(captured), esp_err_to_name(result));
    return result == ESP_OK && captured == pcm.size();
}

bool replay()
{
    i2s_chan_handle_t tx = nullptr;
    size_t preloaded = 0, written = 0, padded = 0;
    esp_err_t result = initI2s(tx, false);
    if (result == ESP_OK) result = i2s_channel_preload_data(tx, pcm.data(), pcm.size(), &preloaded);
    if (result == ESP_OK && !reg(amp, ampOutput, 0xff)) result = ESP_FAIL;
    if (result == ESP_OK) result = i2s_channel_enable(tx);
    if (result == ESP_OK) {
        std::puts("Echo I2S1 TX start");
        result = i2s_channel_write(tx, pcm.data() + preloaded, pcm.size() - preloaded, &written, 2000);
        // Queue one DMA ring of silence to let the last PCM block reach the wire.
        for (unsigned i = 0; i < dmaCount && result == ESP_OK; ++i) {
            result = i2s_channel_write(tx, silence.data(), silence.size(), &padded, 2000);
        }
        i2s_channel_disable(tx);
    }
    if (tx) i2s_del_channel(tx);
    std::printf("Echo playback %u bytes (%s)\n", unsigned(preloaded + written), esp_err_to_name(result));
    return result == ESP_OK && preloaded + written == pcm.size();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Speaker.end();
    M5.Mic.end();
    if (initCodec() && reg(amp, ampOutput, 0x00) && capture() && replay()) {
        std::puts("Echo record and playback complete");
    } else {
        reg(amp, ampOutput, 0x00);
        std::puts("Echo stopped safely");
    }
    while (true) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
