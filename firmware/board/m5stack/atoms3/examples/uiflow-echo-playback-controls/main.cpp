// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>

namespace {
constexpr uint8_t codec = 0x18;
constexpr uint8_t amp = 0x43;
constexpr uint8_t ampOutput = 0x05;
constexpr uint32_t rate = 16000;
constexpr unsigned dmaFrames = 250;
constexpr unsigned dmaCount = 6;
constexpr unsigned blockSize = dmaFrames * 2 * sizeof(int16_t);
constexpr unsigned pauseFrame = rate;
constexpr unsigned totalFrames = rate * 5 / 2;

// Original synthetic WAV corresponding to the upstream logical Flash resource.
struct Wav {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t riffSize = 36 + totalFrames * 2;
    char waveFmt[8] = {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    uint32_t formatSize = 16;
    uint16_t format = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = rate;
    uint32_t byteRate = rate * 2;
    uint16_t blockAlign = 2;
    uint16_t bits = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = totalFrames * 2;
    std::array<int16_t, totalFrames> frames{};
};
static_assert(offsetof(Wav, frames) == 44);
static_assert(pauseFrame % dmaFrames == 0 && totalFrames % dmaFrames == 0);

constexpr Wav makeWav()
{
    Wav wav;
    constexpr int periods[] = {40, 20, 10, 32, 16};
    for (unsigned frame = 0; frame < totalFrames; ++frame) {
        const int period = periods[frame / (rate / 2)];
        const int phase = int(frame % unsigned(period));
        const int triangle = phase < period / 2 ? phase : period - phase;
        const int16_t sample = int16_t(24000 * triangle / period - 6000);
        wav.frames[frame] = sample;
    }
    return wav;
}
constexpr Wav flashWav = makeWav();

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
           reg(amp, 0x03, 0x6e) && reg(amp, ampOutput, 0x00);
}

esp_err_t initI2s(i2s_chan_handle_t& channel)
{
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cfg.dma_frame_num = dmaFrames;
    cfg.dma_desc_num = dmaCount;
    cfg.auto_clear = true;
    esp_err_t result = i2s_new_channel(&cfg, &channel, nullptr);
    if (result != ESP_OK) return result;
    i2s_std_config_t bus{};
    bus.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    bus.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    bus.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    bus.gpio_cfg = {GPIO_NUM_NC, GPIO_NUM_8, GPIO_NUM_6,
                    GPIO_NUM_5, GPIO_NUM_NC, {}};
    return i2s_channel_init_std_mode(channel, &bus);
}

bool IRAM_ATTR sent(i2s_chan_handle_t, i2s_event_data_t*, void* task)
{
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(task), &wake);
    return wake == pdTRUE;
}

void show(const char* status, unsigned cursor)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 12);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.println("Echo Base WAV");
    M5.Display.setCursor(4, 44);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.println(status);
    M5.Display.setCursor(4, 76);
    M5.Display.printf("Frame: %u", cursor);
    std::printf("Echo %s frame=%u us=%lld\n", status, cursor, static_cast<long long>(esp_timer_get_time()));
}

bool playTo(unsigned& cursor, unsigned limit)
{
    i2s_chan_handle_t tx = nullptr;
    const size_t bytes = (limit - cursor) * 4;
    std::array<int16_t, dmaFrames * 2> stereo;
    const auto fill = [&](unsigned start) {
        for (unsigned i = 0; i < dmaFrames; ++i) {
            stereo[2 * i] = flashWav.frames[start + i];
            stereo[2 * i + 1] = stereo[2 * i];
        }
    };
    const auto* data = reinterpret_cast<const uint8_t*>(stereo.data());
    i2s_event_callbacks_t callbacks{};
    callbacks.on_sent = sent;
    ulTaskNotifyTake(pdTRUE, 0);
    esp_err_t result = initI2s(tx);
    if (result == ESP_OK) result = i2s_channel_register_event_callback(tx, &callbacks, xTaskGetCurrentTaskHandle());
    unsigned queued = cursor;
    for (unsigned i = 0; i < dmaCount && result == ESP_OK; ++i) {
        fill(queued);
        size_t loaded = 0;
        result = i2s_channel_preload_data(tx, data, blockSize, &loaded);
        if (result == ESP_OK && loaded != blockSize) result = ESP_FAIL;
        queued += dmaFrames;
    }
    if (result == ESP_OK && !reg(amp, ampOutput, 0xff)) result = ESP_FAIL;
    if (result == ESP_OK) result = i2s_channel_enable(tx);
    const bool enabled = result == ESP_OK;
    while (queued < limit && result == ESP_OK) {
        fill(queued);
        size_t written = 0;
        result = i2s_channel_write(tx, data, blockSize, &written, 250);
        if (result == ESP_OK && written != blockSize) result = ESP_FAIL;
        queued += dmaFrames;
    }
    // Writes queue DMA buffers. Advance the cursor only after their EOF events.
    for (size_t done = 0; result == ESP_OK && done < bytes; done += blockSize) {
        if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(250)) == 0) result = ESP_ERR_TIMEOUT;
        else cursor += dmaFrames;
    }
    if (enabled) i2s_channel_disable(tx);
    if (tx) i2s_del_channel(tx);
    const bool muted = reg(amp, ampOutput, 0x00);
    return muted && result == ESP_OK && cursor == limit;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_display_value = 0;
    M5.begin(config);
    M5.Speaker.end();
    M5.Mic.end();
    unsigned cursor = 0;
    if (initCodec()) {
        show("Playing", cursor);
        if (playTo(cursor, pauseFrame)) {
            show("Paused", cursor);
            vTaskDelay(pdMS_TO_TICKS(1000));
            show("Resuming", cursor);
            if (playTo(cursor, totalFrames)) {
                show("Complete", cursor);
                return;
            }
        }
    }
    show("Echo unavailable", cursor);
}
