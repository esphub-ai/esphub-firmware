// https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/examples/Basic/Microphone/Microphone.ino
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
constexpr size_t blockSamples = 128;
constexpr size_t blockCount = 400;

bool startMic()
{
    if (!M5.Mic.begin()) {
        std::puts("Microphone unavailable");
        return false;
    }
    std::puts("Recording ready: click A to play recent audio");
    return true;
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto recent = std::unique_ptr<int16_t[]>(new (std::nothrow) int16_t[blockSamples * blockCount]);
    if (!recent) {
        std::puts("Recording memory unavailable");
        return;
    }

    M5.Speaker.setVolume(255);
    M5.Speaker.end();
    auto config = M5.Mic.config();
    config.sample_rate = sampleRate;
    config.dma_buf_len = blockSamples;
    M5.Mic.config(config);
    if (!startMic()) {
        return;
    }

    size_t next = 0;
    size_t count = 0;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasHold()) {
            auto mic = M5.Mic.config();
            mic.noise_filter_level = static_cast<uint8_t>(mic.noise_filter_level + 8);
            M5.Mic.config(mic);
            std::printf("Microphone noise filter: %u\n", mic.noise_filter_level);
        } else if (count && M5.BtnA.wasClicked()) {
            while (M5.Mic.isRecording()) {
                vTaskDelay(1);
            }
            M5.Mic.end();
            if (count == blockCount) {
                std::rotate(recent.get(), recent.get() + next * blockSamples,
                            recent.get() + count * blockSamples);
            }

            const size_t samples = count * blockSamples;
            if (M5.Speaker.begin() &&
                M5.Speaker.playRaw(recent.get(), samples, sampleRate, false, 1, 0)) {
                std::printf("Playback started: %u samples\n", static_cast<unsigned>(samples));
                while (M5.Speaker.isPlaying()) {
                    M5.update();
                    vTaskDelay(1);
                }
                std::puts("Playback complete");
            } else {
                std::puts("Playback unavailable");
            }
            M5.Speaker.end();
            next = 0;
            count = 0;
            if (!startMic()) {
                return;
            }
        }

        if (M5.Mic.record(recent.get() + next * blockSamples, blockSamples, sampleRate)) {
            next = (next + 1) % blockCount;
            count = std::min(count + 1, blockCount);
        } else {
            vTaskDelay(1);
        }
    }
}
