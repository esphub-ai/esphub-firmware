// https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/examples/Advanced/Mic_FFT/Mic_FFT.ino
#include "eh_agent.h"
#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr size_t sampleRate = 24000;
constexpr size_t samples = 1024;
constexpr size_t bands = 16;
constexpr float pi = 3.14159265358979323846f;
alignas(16) static float fft[2 * samples];
static float window[samples];
static int16_t pcm[samples];

int levelDb(float amplitude)
{
    return static_cast<int>(std::lround(20.0f * std::log10(std::max(amplitude, 0.00001f))));
}

void analyze()
{
    float mean = 0;
    for (const auto value : pcm) {
        mean += value;
    }
    mean /= samples;
    for (size_t i = 0; i < samples; ++i) {
        fft[2 * i] = (pcm[i] - mean) * window[i] / 32768.0f;
        fft[2 * i + 1] = 0;
    }
    ESP_ERROR_CHECK(dsps_fft2r_fc32(fft, samples));
    ESP_ERROR_CHECK(dsps_bit_rev_fc32(fft, samples));

    float bandLevels[bands]{};
    float peak = 0;
    size_t peakBin = 0;
    for (size_t bin = 1; bin < samples / 2; ++bin) {
        const float level = 4.0f * std::hypot(fft[2 * bin], fft[2 * bin + 1]) / samples;
        auto& band = bandLevels[bin * bands / (samples / 2)];
        band = std::max(band, level);
        if (level > peak) {
            peak = level;
            peakBin = bin;
        }
    }
    if (peak < 0.00001f) {
        std::printf("FFT quiet;");
    } else {
        std::printf("FFT peak=%.1f Hz bin=%u level=%d dBFS;",
                    static_cast<double>(sampleRate) * peakBin / samples,
                    static_cast<unsigned>(peakBin), levelDb(peak));
    }
    std::printf(" bands(dBFS):");
    for (const auto band : bandLevels) {
        std::printf(" %d", levelDb(band));
    }
    std::putchar('\n');
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Speaker.end();
    auto mic = M5.Mic.config();
    mic.sample_rate = sampleRate;
    mic.dma_buf_len = 256;
    M5.Mic.config(mic);
    if (!M5.Mic.begin()) {
        std::puts("FFT microphone unavailable");
        return;
    }

    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(nullptr, samples));
    for (size_t i = 0; i < samples; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2 * pi * i / samples);
    }
    std::puts("Mic FFT ready: 24000 Hz, 1024 samples, 16 bands from 0 to 12000 Hz");

    for (;;) {
        M5.update();
        if (M5.Mic.record(pcm, samples, sampleRate)) {
            while (M5.Mic.isRecording()) {
                vTaskDelay(1);
            }
            analyze();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
