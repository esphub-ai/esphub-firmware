// https://github.com/m5stack/M5Unified/blob/96576a14247ddc935149145aa6b51943464120f1/examples/Basic/Speaker/Speaker.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace {
constexpr int playbackChannel = 0;
constexpr int toneChannel = 1;

enum class Menu { tone, playback, masterUp, masterDown, channelUp, channelDown, count };
constexpr const char* menuNames[] = {
    "Tone", "Play/stop", "Master volume up", "Master volume down",
    "Channel volume up", "Channel volume down",
};

// Original 16 kHz, 16-bit mono WAV, with 400 Hz and 800 Hz segments.
constexpr auto makeWave()
{
    constexpr int frames = 8000;
    std::array<uint8_t, 44 + 2 * frames> wav{};
    constexpr uint8_t header[] = {
        'R','I','F','F', 0xa4,0x3e,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0,1,0, 0x80,0x3e,0,0, 0,0x7d,0,0,
        2,0,16,0, 'd','a','t','a', 0x80,0x3e,0,0,
    };
    std::copy(std::begin(header), std::end(header), wav.begin());
    for (int i = 0; i < frames; ++i) {
        const int period = i < frames / 2 ? 40 : 20;
        const int phase = i % period;
        const int slope = phase < period / 2 ? phase : period - phase;
        const auto bits = static_cast<uint16_t>(40000 * slope / period - 10000);
        wav[44 + 2 * i] = bits & 0xff;
        wav[45 + 2 * i] = bits >> 8;
    }
    return wav;
}

alignas(4) constexpr auto wave = makeWave();

void playLoop()
{
    if (M5.Speaker.playWav(wave.data(), wave.size(), ~0u, playbackChannel, true)) {
        std::puts("Playback started");
    } else {
        std::puts("Playback unavailable");
    }
}

void adjustVolume(Menu menu)
{
    const bool master = menu == Menu::masterUp || menu == Menu::masterDown;
    const int step = menu == Menu::masterUp || menu == Menu::channelUp ? 1 : -1;
    const int volume = master ? M5.Speaker.getVolume() : M5.Speaker.getChannelVolume(playbackChannel);
    const auto next = static_cast<uint8_t>(std::clamp(volume + step, 0, 255));
    if (master) {
        M5.Speaker.setVolume(next);
    } else {
        M5.Speaker.setChannelVolume(playbackChannel, next);
    }
}

void showVolumes()
{
    std::printf("Volume: master=%u channel0=%u\n", M5.Speaker.getVolume(),
                M5.Speaker.getChannelVolume(playbackChannel));
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Mic.end();
    if (!M5.Speaker.begin()) {
        std::puts("Speaker unavailable");
        return;
    }

    M5.Speaker.setVolume(64);
    M5.Speaker.setChannelVolume(playbackChannel, 255);
    std::puts("Speaker ready: click A to select, hold A to act");
    showVolumes();
    playLoop();
    auto menu = Menu::tone;
    std::puts("Selected: Tone");
    int toneHz = 100;

    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            const int next = (static_cast<int>(menu) + 1) % static_cast<int>(Menu::count);
            menu = static_cast<Menu>(next);
            std::printf("Selected: %s\n", menuNames[next]);
        }
        if (M5.BtnA.isHolding()) {
            switch (menu) {
            case Menu::tone:
                if (M5.BtnA.wasHold()) {
                    toneHz = 100;
                    std::puts("Tone sweep started");
                }
                M5.Speaker.tone(++toneHz, 1000, toneChannel);
                break;
            case Menu::playback:
                if (M5.BtnA.wasHold()) {
                    if (M5.Speaker.isPlaying(playbackChannel)) {
                        M5.Speaker.stop(playbackChannel);
                        std::puts("Playback stopped");
                    } else {
                        playLoop();
                    }
                }
                break;
            default:
                adjustVolume(menu);
                break;
            }
        }
        if (M5.BtnA.wasReleasedAfterHold() && menu >= Menu::masterUp) {
            showVolumes();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
