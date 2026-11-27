// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr unsigned rate = 24000;
constexpr unsigned blockSize = 256;
constexpr unsigned fftSize = 1024;
constexpr unsigned width = 128;
constexpr unsigned rowHeight = 32;
constexpr float pi = 3.14159265358979323846f;
constexpr uint8_t codec = 0x18;
constexpr uint8_t amplifier = 0x43;
std::array<std::array<int16_t, blockSize>, 2> dmaBlocks;
std::array<int16_t, fftSize> samples{};
std::array<float, fftSize> real, imaginary, window;
std::array<float, fftSize / 2 + 1> magnitude;
std::array<uint8_t, width * rowHeight> history{};
unsigned nextSample = 0;

void transform()
{
    float mean = 0;
    for (auto sample : samples) mean += sample;
    mean /= fftSize;

    // Store the windowed input in bit-reversed order for the radix-2 FFT.
    for (unsigned i = 0, j = 0; i < fftSize; ++i) {
        real[j] = (samples[(nextSample + i) % fftSize] - mean) * window[i];
        imaginary[j] = 0;
        for (unsigned bit = fftSize / 2; bit; bit >>= 1) {
            j ^= bit;
            if (j & bit) break;
        }
    }
    for (unsigned size = 2; size <= fftSize; size *= 2) {
        for (unsigned j = 0; j < size / 2; ++j) {
            const float angle = -2 * pi * j / size;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            for (unsigned i = j; i < fftSize; i += size) {
                const unsigned k = i + size / 2;
                const float a = real[k] * c - imaginary[k] * s;
                const float b = real[k] * s + imaginary[k] * c;
                real[k] = real[i] - a;
                imaginary[k] = imaginary[i] - b;
                real[i] += a;
                imaginary[i] += b;
            }
        }
    }
    for (unsigned i = 0; i < magnitude.size(); ++i)
        magnitude[i] = std::sqrt(real[i] * real[i] + imaginary[i] * imaginary[i]) * (4.0f / fftSize);
}

void drawFrame()
{
    constexpr uint16_t spectrumColor = 0x000c;
    constexpr uint16_t waveColor = 0x0200;
    auto& lcd = M5.Display;
    const auto peak = std::max_element(magnitude.begin() + 1, magnitude.end());
    const unsigned bin = peak - magnitude.begin();
    const bool silent = *peak < 8;
    const float hz = silent ? 0 : float(bin * rate) / fftSize;
    const auto newest = (nextSample + fftSize - width) % fftSize;

    std::memmove(history.data() + width, history.data(), history.size() - width);
    lcd.startWrite();
    lcd.fillRect(0, 0, width, rowHeight, TFT_BLACK);
    lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    lcd.setCursor(4, 7);
    lcd.setTextSize(2);
    if (silent) lcd.print("Silence");
    else lcd.printf("%.1f Hz", hz);

    lcd.fillRect(0, rowHeight, width, rowHeight, spectrumColor);
    constexpr unsigned binsPerColumn = fftSize / 2 / width;
    for (unsigned x = 0; x < width; ++x) {
        const auto first = magnitude.begin() + 1 + x * binsPerColumn;
        const float value = *std::max_element(first, first + binsPerColumn);
        const int bar = std::clamp(int(std::sqrt(value) * 0.28f), 0, int(rowHeight - 1));
        history[x] = uint8_t(bar * 255 / (rowHeight - 1));
        if (bar) lcd.drawFastVLine(x, 2 * rowHeight - bar, bar, TFT_GREEN);
    }
    lcd.pushGrayscaleImage(0, 2 * rowHeight, width, rowHeight, history.data(),
                           m5gfx::color_depth_t::grayscale_8bit, TFT_YELLOW, TFT_BLACK);

    lcd.fillRect(0, 3 * rowHeight, width, rowHeight, TFT_BLACK);
    constexpr int middle = 3 * rowHeight + rowHeight / 2;
    lcd.drawFastHLine(0, middle, width, waveColor);
    int previous = middle;
    int16_t low = 0, high = 0;
    for (unsigned x = 0; x < width; ++x) {
        const auto sample = samples[(newest + x) % fftSize];
        low = std::min(low, sample);
        high = std::max(high, sample);
        const int y = middle - (int(sample) * (int(rowHeight) / 2 - 1) / 32768);
        if (x) lcd.drawLine(x - 1, previous, x, y, TFT_WHITE);
        previous = y;
    }
    lcd.endWrite();

    static int lastBin = -1;
    const int reportedBin = silent ? 0 : int(bin);
    if (lastBin != reportedBin) {
        std::printf("FFT peak=%.1f Hz bin=%d level=%.1f wave=%d..%d\n",
                    hz, reportedBin, *peak, int(low), int(high));
        lastBin = reportedBin;
    }
}

void showUnavailable()
{
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(true);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.println("microphone is not available.");
    M5_LOGE("microphone is not available.");
}

bool startMicrophone()
{
    auto& i2c = M5.In_I2C;
    // The pinned Echo callbacks configure the codec but don't propagate NACK.
    if (!i2c.scanID(codec, 100000) || !i2c.scanID(amplifier, 100000) || !M5.Mic.isEnabled())
        return false;
    const auto speaker = M5.Speaker.config();
    std::printf("Echo Base detected; Speaker I2S%d BCK=%d WS=%d DO=%d; tone 440 Hz 100 ms\n",
                int(speaker.i2s_port), int(speaker.pin_bck), int(speaker.pin_ws), int(speaker.pin_data_out));
    const bool tone = M5.Speaker.tone(440, 100);
    if (tone) {
        M5.delay(100);
        const uint32_t deadline = M5.millis() + 900;
        while (M5.Speaker.isPlaying() && int32_t(deadline - M5.millis()) > 0) vTaskDelay(1);
    }
    const bool complete = tone && !M5.Speaker.isPlaying();
    if (complete) {
        // isPlaying() measures the producer; the TX ring can still hold tone samples.
        const auto drainMs = (1000 * speaker.dma_buf_len * speaker.dma_buf_count +
                              speaker.sample_rate - 1) / speaker.sample_rate;
        M5.delay(drainMs);
    }
    M5.Speaker.end();
    if (!complete) return false;

    auto mic = M5.Mic.config();
    mic.dma_buf_count = 3;
    mic.dma_buf_len = blockSize;
    mic.over_sampling = 1;
    mic.noise_filter_level = 0;
    mic.sample_rate = rate;
    mic.magnification = mic.use_adc ? 16 : 1;
    M5.Mic.config(mic);
    if (!M5.Mic.begin()) return false;
    std::printf("Mic I2S%d BCK=%d WS=%d DI=%d %u Hz DMA=%u FFT=%u\n",
                int(mic.i2s_port), int(mic.pin_bck), int(mic.pin_ws), int(mic.pin_data_in), rate, blockSize, fftSize);
    return true;
}

void runAnalyzer()
{
    for (unsigned i = 0; i < fftSize; ++i) window[i] = 0.5f - 0.5f * std::cos(2 * pi * i / fftSize);
    for (auto& block : dmaBlocks)
        if (!M5.Mic.record(block.data(), block.size(), rate)) return;

    unsigned blocks = 0;
    unsigned next = 0;
    for (;;) {
        const uint32_t deadline = M5.millis() + 1000;
        while (M5.Mic.isRecording() >= dmaBlocks.size()) {
            if (int32_t(deadline - M5.millis()) <= 0) return;
            vTaskDelay(1);
        }
        auto& block = dmaBlocks[next];
        std::copy(block.begin(), block.end(), samples.begin() + nextSample);
        nextSample = (nextSample + blockSize) % fftSize;
        if (!M5.Mic.record(block.data(), block.size(), rate)) return;
        next ^= 1;
        // Display one transform per complete window while RX continues.
        if (++blocks == fftSize / blockSize) {
            blocks = 0;
            transform();
            drawFrame();
        }
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_speaker.atomic_echo = true;
    config.external_display_value = 0;
    M5.begin(config);
    M5.Display.fillScreen(TFT_BLACK);
    if (startMicrophone()) {
        runAnalyzer();
        M5.Mic.end();
    }
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    showUnavailable();
}
