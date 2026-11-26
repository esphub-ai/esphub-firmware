// https://docs.m5stack.com/en/arduino/papercolor/mic
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>

namespace {
constexpr size_t sampleRate = 16000;
constexpr size_t blockSamples = 200;
constexpr size_t blocks = 256;

void draw(const char* status)
{
    auto& display = M5.Display;
    const int center = display.width() / 2;
    const int middle = display.height() / 2;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString("AUDIO RECORDER", center, 40);
    if (status) {
        display.setTextColor(RED);
        display.drawString(status, center, middle);
    } else {
        display.setTextColor(RED);
        display.drawString("Press Button A", center, middle - 50);
        display.setTextColor(BLUE);
        display.drawString("to Record", center, middle);
        display.setTextColor(GREEN);
        display.drawString("Release to Playback", center, middle + 50);
    }
    display.endWrite();
}

bool startMic()
{
    if (M5.Mic.begin()) {
        draw(nullptr);
        std::puts("Audio recorder ready");
        return true;
    }
    draw("Microphone unavailable");
    return false;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setRotation(0);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    auto audio = std::unique_ptr<int16_t[]>(new (std::nothrow) int16_t[blocks * blockSamples]);
    if (!audio) {
        draw("Recording memory unavailable");
        return;
    }

    M5.Speaker.setVolume(255);
    auto micConfig = M5.Mic.config();
    micConfig.sample_rate = sampleRate;
    // Mono capture reads two DMA descriptors per request.
    micConfig.dma_buf_len = blockSamples / 2;
    M5.Mic.config(micConfig);
    M5.Speaker.end();
    if (!startMic()) {
        return;
    }

    bool recording = false;
    size_t nextBlock = 0;
    size_t usedBlocks = 0;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            nextBlock = 0;
            usedBlocks = 0;
            recording = true;
            draw("Recording...");
            std::puts("Recording started");
        }
        if (recording && M5.BtnA.wasReleased()) {
            recording = false;
            while (M5.Mic.isRecording()) {
                vTaskDelay(1);
            }
            M5.Mic.end();

            // A long hold retains the latest blocks. Play them oldest first.
            if (usedBlocks == blocks) {
                std::rotate(audio.get(), audio.get() + nextBlock * blockSamples,
                            audio.get() + blocks * blockSamples);
            }
            const size_t captured = usedBlocks * blockSamples;
            if (captured && M5.Speaker.begin()) {
                draw("Playing recording...");
                if (M5.Speaker.playRaw(audio.get(), captured, sampleRate, false, 1, 0)) {
                    std::printf("Playback started: %u samples\n", static_cast<unsigned>(captured));
                    while (M5.Speaker.isPlaying()) {
                        M5.update();
                        vTaskDelay(1);
                    }
                    std::puts("Playback complete");
                }
            }
            M5.Speaker.end();
            if (!startMic()) {
                return;
            }
        }
        if (recording && M5.Mic.record(audio.get() + nextBlock * blockSamples, blockSamples, sampleRate)) {
            nextBlock = (nextBlock + 1) % blocks;
            usedBlocks = std::min(usedBlocks + 1, blocks);
            // Keep the next DMA request queued while the current block is filled.
            continue;
        }
        vTaskDelay(1);
    }
}
