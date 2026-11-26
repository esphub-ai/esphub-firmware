// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/mic/mic.ino
#include <M5Unified.h>
#include "eh_agent.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    constexpr unsigned rate = 16000;
    static int16_t sample[rate / 2]; // Half a second fits in internal RAM.
    M5.Display.println("BtnA: record and play");
    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            M5.Display.clear();
            M5.Display.setCursor(0, 0);
            M5.Display.println("Recording...");
            M5.Speaker.end();
            if (!M5.Mic.begin() || !M5.Mic.record(sample, rate / 2, rate)) {
                M5.Display.println("Microphone unavailable");
                return;
            }
            while (M5.Mic.isRecording()) M5.delay(10);
            M5.Mic.end();

            M5.Display.clear();
            const int middle = M5.Display.height() / 2;
            for (int x = 0; x < M5.Display.width(); ++x) {
                const size_t i = x * (rate / 2) / M5.Display.width();
                M5.Display.drawLine(x, middle, x, middle + sample[i] * middle / 32768, TFT_GREEN);
            }
            M5.Display.setCursor(0, 0);
            M5.Display.println("Playing | BtnA: record again");
            M5.Speaker.setVolume(96);
            if (!M5.Speaker.begin() || !M5.Speaker.playRaw(sample, rate / 2, rate)) {
                M5.Display.println("Speaker unavailable");
                return;
            }
            while (M5.Speaker.isPlaying()) M5.delay(10);
            M5.Speaker.end();
        }
        M5.delay(10);
    }
}
