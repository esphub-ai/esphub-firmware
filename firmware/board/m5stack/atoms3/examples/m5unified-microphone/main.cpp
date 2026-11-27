// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "esp_heap_caps.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
constexpr unsigned rate = 16000;
constexpr unsigned blockSize = 200;
constexpr unsigned blockCount = 256;
constexpr unsigned sampleCount = blockSize * blockCount;
constexpr unsigned width = 128;
int16_t* ring;
std::atomic<bool> keepRecording{false};
std::atomic<bool> captureStopped{true};
std::atomic<bool> filterRequested{false};
std::atomic<unsigned> filterLevel{0};
std::atomic<unsigned> completed{0};
std::atomic<const int16_t*> latest{nullptr};
std::array<int, width> previousY{}, previousH{};

bool queueCapture(unsigned index)
{
    return M5.Mic.record(ring + (index % blockCount) * blockSize, blockSize, rate);
}

void released(void*, void* data, size_t)
{
    const auto done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
    latest.store(static_cast<const int16_t*>(data), std::memory_order_release);
    if (filterRequested.exchange(false)) {
        auto cfg = M5.Mic.config();
        cfg.noise_filter_level = (cfg.noise_filter_level + 8) & 255;
        M5.Mic.config(cfg);
        filterLevel.store(cfg.noise_filter_level, std::memory_order_release);
    }
    // Refill the freed request before the audio task goes idle. Slow LCD
    // transfers must not stall capture or drop the unused part of a DMA block.
    if (!keepRecording.load(std::memory_order_acquire) || !queueCapture(done))
        captureStopped.store(true, std::memory_order_release);
}

bool startCapture()
{
    if (!M5.Mic.begin()) return false;
    captureStopped.store(false);
    keepRecording.store(true);
    if (queueCapture(completed.load(std::memory_order_acquire))) return true;
    M5.Mic.end();
    return false;
}

void status(const char* label)
{
    auto& lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(2, 2);
    lcd.print(label);
    previousH.fill(0);
    if (std::strcmp(label, "REC") == 0) {
        lcd.setCursor(32, 2);
        lcd.printf("nf:%03u", filterLevel.load(std::memory_order_acquire));
    }
}

void plot(const int16_t* data)
{
    auto& lcd = M5.Display;
    lcd.startWrite();
    for (unsigned x = 0; x < width; ++x) {
        if (previousH[x]) lcd.writeFastVLine(x, previousY[x], previousH[x], TFT_BLACK);
        const int a = std::clamp(70 + (data[x] >> 6), 16, 127);
        const int b = std::clamp(70 + (data[x + 1] >> 6), 16, 127);
        previousY[x] = std::min(a, b);
        previousH[x] = std::max(a, b) - previousY[x] + 1;
        lcd.writeFastVLine(x, previousY[x], previousH[x], TFT_WHITE);
    }
    lcd.endWrite();
    lcd.display();
}

bool replay()
{
    const auto deadline = M5.millis() + 800;
    keepRecording.store(false, std::memory_order_release);
    while (!captureStopped.load(std::memory_order_acquire)) {
        if (int32_t(deadline - M5.millis()) <= 0) return false;
        vTaskDelay(1);
    }
    M5.Mic.end();
    latest.exchange(nullptr);
    const auto done = completed.load(std::memory_order_acquire);
    const auto start = done % blockCount * blockSize;
    status("PLAY");
    std::printf("PLAY completed=%u start=%u tail=%u head=%u\n",
                done, start, sampleCount - start, start);
    auto& speaker = M5.Speaker;
    if (!speaker.begin()) return false;
    bool queued = speaker.playRaw(ring + start, sampleCount - start, rate, false, 1, 0);
    if (queued && start) queued = speaker.playRaw(ring, start, rate, false, 1, 0);
    // Queue silence behind the data so the mono mixer's final word alignment
    // flushes a padding sample instead of the last recorded sample.
    static constexpr int16_t padding[2]{};
    if (queued) queued = speaker.playRaw(padding, std::size(padding), rate, false, 1, 0);
    const auto playbackDeadline = M5.millis() + 6000;
    while (speaker.isPlaying() && int32_t(playbackDeadline - M5.millis()) > 0) {
        M5.update();
        vTaskDelay(1);
    }
    const bool finished = queued && !speaker.isPlaying();
    if (finished) M5.delay(160); // At 16 kHz the eight 256-frame descriptors hold 128 ms.
    speaker.end();
    if (!finished || !startCapture()) return false;
    status("REC");
    std::puts("REC resumed");
    return true;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_speaker.atomic_echo = true;
    config.external_display_value = 0;
    M5.begin(config);
    M5.Speaker.end();
    if (!M5.In_I2C.scanID(0x18, 100000) || !M5.In_I2C.scanID(0x43, 100000)
        || !M5.Mic.isEnabled() || !M5.Speaker.isEnabled()) {
        status("Echo unavailable");
        std::puts("Echo unavailable");
        return;
    }

    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ring = static_cast<int16_t*>(heap_caps_calloc(sampleCount, sizeof(*ring), caps));
    std::printf("Ring bytes=%u internal_free=%u largest=%u\n", sampleCount * unsigned(sizeof(*ring)),
                unsigned(heap_caps_get_free_size(caps)), unsigned(heap_caps_get_largest_free_block(caps)));
    if (!ring) {
        status("Audio memory unavailable");
        return;
    }

    auto mic = M5.Mic.config();
    mic.sample_rate = rate;
    mic.dma_buf_len = 256;
    mic.dma_buf_count = 3;
    mic.over_sampling = 1;
    mic.magnification = 2;
    mic.noise_filter_level = 0;
    M5.Mic.config(mic);
    auto speaker = M5.Speaker.config();
    speaker.sample_rate = rate;
    speaker.dma_buf_len = 256;
    speaker.magnification = 1;
    M5.Speaker.config(speaker);
    M5.Speaker.setVolume(255);
    M5.Speaker.setAllChannelVolume(255);
    M5.Mic.setBufferReleaseCallback(nullptr, released);
    if (!startCapture()) {
        status("Mic unavailable");
        heap_caps_free(ring);
        return;
    }
    status("REC");
    std::puts("REC started I2S1 16000 Hz");
    unsigned previousReport = 0;
    unsigned displayedFilter = 0;
    for (;;) {
        M5.update();
        const auto done = completed.load(std::memory_order_acquire);
        if (captureStopped.load(std::memory_order_acquire)) {
            M5.Mic.end();
            status("Capture unavailable");
            heap_caps_free(ring);
            return;
        }
        if (const auto* data = latest.exchange(nullptr, std::memory_order_acq_rel)) {
            plot(data);
            if (done - previousReport >= 24) {
                unsigned meanAbs = 0;
                for (unsigned i = 0; i < blockSize; ++i) meanAbs += std::abs(int(data[i]));
                std::printf("REC block=%u nf=%u meanabs=%u internal_free=%u\n", done,
                            filterLevel.load(std::memory_order_acquire), meanAbs / blockSize,
                            unsigned(heap_caps_get_free_size(caps)));
                previousReport = done;
            }
        }
        const auto filter = filterLevel.load(std::memory_order_acquire);
        if (filter != displayedFilter) {
            displayedFilter = filter;
            M5.Display.setCursor(32, 2);
            M5.Display.printf("nf:%03u", filter);
            std::printf("Filter nf:%03u\n", filter);
        }
        if (M5.BtnA.wasHold()) {
            filterRequested.store(true, std::memory_order_release);
        } else if (M5.BtnA.wasClicked() && !replay()) {
            M5.Mic.end();
            status("Audio unavailable");
            std::puts("Audio unavailable");
            heap_caps_free(ring);
            return;
        }
        vTaskDelay(1);
    }
}
