// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace {
constexpr const char* menu[] = {"tone", "play/stop", "ms vol u", "ms vol d", "ch vol u", "ch vol d"};
constexpr unsigned menuCount = std::size(menu);
unsigned selection = 0;
unsigned toneHz = 100;

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

constexpr auto makeClick()
{
    std::array<int16_t, 2205> pcm{};
    for (unsigned i = 0; i < pcm.size(); ++i) {
        const int phase = i % 15;
        pcm[i] = int16_t((phase < 7 ? 1 : -1) * int(pcm.size() - i) * 8);
    }
    return pcm;
}
constexpr auto click = makeClick();

constexpr auto makeCustomWave()
{
    std::array<uint8_t, 64> output{};
    for (unsigned i = 0; i < output.size(); ++i)
        output[i] = uint8_t(32 + 6 * (i < 32 ? i : 63 - i));
    return output;
}
constexpr auto customWave = makeCustomWave();

void playClick()
{
    M5.Speaker.playRaw(click.data(), click.size(), 44100, false, 1, 4, true);
}

void drawVolumes()
{
    auto& lcd = M5.Display;
    const uint8_t volumes[] = {M5.Speaker.getVolume(), M5.Speaker.getChannelVolume(0)};
    lcd.fillRect(110, 0, 18, 128, TFT_BLACK);
    for (unsigned i = 0; i < std::size(volumes); ++i) {
        const int x = 111 + 8 * i;
        const int height = volumes[i] * 124 / 255;
        lcd.drawRect(x, 1, 7, 126, TFT_DARKGREY);
        if (height) lcd.fillRect(x + 1, 126 - height, 5, height, i == 0 ? TFT_CYAN : TFT_GREEN);
    }
}

void redraw()
{
    auto& lcd = M5.Display;
    lcd.startWrite();
    lcd.setFont(&fonts::DejaVu18);
    lcd.setTextDatum(textdatum_t::middle_center);
    for (unsigned i = 0; i < menuCount; ++i) {
        const int y = 1 + 21 * i;
        const uint16_t foreground = selection == i ? TFT_BLACK : TFT_WHITE;
        const uint16_t background = selection == i ? TFT_WHITE : TFT_BLACK;
        lcd.fillRect(1, y, 107, 19, background);
        lcd.setTextColor(foreground, background);
        lcd.drawString(menu[i], 54, y + 9);
    }
    drawVolumes();
    lcd.endWrite();
}

void playLoop()
{
    const bool playing = M5.Speaker.playWav(wave.data(), wave.size(), ~0u, 0, true);
    std::puts(playing ? "Loop playing" : "Loop unavailable");
}

void waitAudio()
{
    while (M5.Speaker.isPlaying()) M5.delay(5);
    M5.delay(65); // Allow the queued TX data to leave the DMA ring.
}

void selfTest()
{
    auto& spk = M5.Speaker;
    spk.setVolume(64);
    spk.setAllChannelVolume(255);
    std::puts("Self-test 2000 Hz");
    spk.tone(2000, 100);
    waitAudio();
    std::puts("Self-test 1000 Hz");
    spk.tone(1000, 100);
    waitAudio();
    M5.delay(500);
    std::puts("Self-test click");
    playClick();
    waitAudio();
    M5.delay(500);
    std::puts("Self-test 440 Hz");
    spk.tone(440, 1000);
    waitAudio();
    M5.delay(500);
    std::puts("Self-test 880 Hz fade");
    spk.setVolume(0);
    spk.tone(880);
    for (unsigned i = 0; i <= 64; ++i) {
        spk.setVolume(i);
        M5.delay(25);
    }
    spk.stop();
    waitAudio();
    M5.delay(500);

    constexpr float notes[] = {261.626f, 329.628f, 391.995f};
    for (unsigned sequence = 0; sequence < 3; ++sequence) {
        std::puts(sequence == 0 ? "Self-test override" :
                  sequence == 1 ? "Self-test mix" : "Self-test custom");
        for (unsigned i = 0; i < std::size(notes); ++i) {
            const int channel = sequence == 0 ? 1 : int(i + 1);
            if (sequence == 2)
                spk.tone(notes[i], 1000, channel, true, customWave.data(), customWave.size());
            else
                spk.tone(notes[i], 1000, channel);
            if (i + 1 < std::size(notes)) M5.delay(200);
        }
        waitAudio();
        M5.delay(500);
    }
}

void act(bool initial)
{
    auto& spk = M5.Speaker;
    if (selection == 0) {
        if (initial) toneHz = 100;
        spk.tone(++toneHz, 1000, 1);
        if (initial || toneHz % 16 == 0) std::printf("Tone %u Hz\n", toneHz);
    } else if (selection == 1) {
        if (!initial) return;
        if (spk.isPlaying(0)) {
            spk.stop(0);
            std::puts("Loop stopped");
        } else {
            playLoop();
        }
    } else {
        const bool master = selection < 4;
        const int previous = master ? spk.getVolume() : spk.getChannelVolume(0);
        const int next = std::clamp(previous + (selection % 2 ? -1 : 1), 0, 255);
        if (previous == next) return;
        if (master) spk.setVolume(next);
        else spk.setChannelVolume(0, next);
        M5.Display.startWrite();
        drawVolumes();
        M5.Display.endWrite();
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.external_speaker.atomic_spk = true;
    config.external_display_value = 0;
    config.internal_imu = false;
    config.internal_rtc = false;
    M5.begin(config);
    M5.Mic.end();
    auto speaker = M5.Speaker.config();
    if (speaker.pin_bck != 5 || speaker.pin_ws != 39 || speaker.pin_data_out != 38) {
        M5.Display.print("Speaker not found");
        std::puts("Atomic SPK unavailable");
        return;
    }
    // AtomS3 I2C1 shares GPIO38/39 with Atomic SPK.
    M5.In_I2C.release();
    // M5Unified defaults this route to the highest I2S port. Use the
    // AtomS3 Atomic SPK examples' I2S0 convention after detecting the pins.
    speaker.i2s_port = I2S_NUM_0;
    speaker.stereo = true; // The mono mix feeds both I2S slots.
    M5.Speaker.config(speaker);
    if (!M5.Speaker.begin()) {
        M5.Display.print("Speaker init failed");
        return;
    }
    std::puts("Atomic SPK I2S0 BCLK=5 WS=39 DOUT=38");
    M5.Display.print("SOUND TEST");
    selfTest();
    M5.Display.clear();
    redraw();
    std::puts("Menu tone master=64 channel=255");
    playLoop();

    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            selection = (selection + 1) % menuCount;
            playClick();
            redraw();
            std::printf("Menu %s\n", menu[selection]);
        }
        if (M5.BtnA.wasHold()) act(true);
        else if (M5.BtnA.isHolding()) act(false);
        if (M5.BtnA.wasReleasedAfterHold() && selection >= 2)
            std::printf("Volume master=%u channel=%u\n", M5.Speaker.getVolume(), M5.Speaker.getChannelVolume(0));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
