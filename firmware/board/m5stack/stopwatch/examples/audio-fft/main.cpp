// Adapted from M5Stack's M5StopWatch-UserDemo/app_fft and hal_audio.cpp (MIT).
#include "../common/round_display.hpp"
#include "eh_agent.h"
#include "esp_dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Speaker.end();
    if (!M5.Mic.begin()) {
        centered("Microphone unavailable", 230, 2);
        return;
    }

    constexpr int samples = 512, rate = 16000, bands = 24;
    static int16_t pcm[samples];
    alignas(16) static float window[samples], fft[2 * samples];
    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(nullptr, samples));
    dsps_wind_hann_f32(window, samples);
    std::array<int, bands + 1> edges{};
    edges.front() = 1; // Skip DC; bins above this are grouped logarithmically.
    edges.back() = samples / 2;
    for (int i = 1; i < bands; ++i) {
        const int bin = std::lround(std::pow(samples / 2.0, static_cast<double>(i) / bands));
        edges[i] = std::clamp(bin, edges[i - 1] + 1, samples / 2 - (bands - i));
    }

    centered("Audio FFT", 93, 3);
    for (;;) {
        if (!M5.Mic.record(pcm, samples, rate)) {
            centered("Recording failed", 230, 2);
            return;
        }
        while (M5.Mic.isRecording()) M5.delay(5);
        float mean = 0;
        for (const auto sample : pcm) mean += sample;
        mean /= samples;
        for (int i = 0; i < samples; ++i) {
            fft[2 * i] = (pcm[i] - mean) * window[i] / 32768.f;
            fft[2 * i + 1] = 0;
        }
        ESP_ERROR_CHECK(dsps_fft2r_fc32(fft, samples));
        ESP_ERROR_CHECK(dsps_bit_rev_fc32(fft, samples));
        float peak = 0;
        int peak_bin = 0;
        M5.Display.startWrite();
        for (int band = 0; band < bands; ++band) {
            float strongest = 0;
            for (int bin = edges[band]; bin < edges[band + 1]; ++bin) {
                const float amplitude = std::hypot(fft[2 * bin], fft[2 * bin + 1]);
                strongest = std::max(strongest, amplitude);
                if (amplitude > peak) {
                    peak = amplitude;
                    peak_bin = bin;
                }
            }
            const float db = 20.f * std::log10(std::max(strongest * 4.f / samples, 1.e-5f));
            const int height = std::clamp(static_cast<int>((db + 65.f) * 2.4f), 0, 160);
            const int x = M5.Display.width() / 2 - bands * 7 + 14 * band;
            M5.Display.fillRect(x, 150, 11, 160 - height, TFT_BLACK);
            M5.Display.fillRect(x, 310 - height, 11, height, TFT_CYAN);
        }
        char label[32];
        snprintf(label, sizeof(label), "Peak %4d Hz", peak_bin * rate / samples);
        centered(label, 355, 2);
        M5.Display.endWrite();
    }
}
