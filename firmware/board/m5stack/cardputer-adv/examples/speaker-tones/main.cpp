// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/buzzer/buzzer.ino
#include <M5Unified.h>
#include "eh_agent.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Speaker.setVolume(64);
    M5.Display.println("Speaker tones: 10 kHz / 4 kHz");
    for (;;) {
        M5.Speaker.tone(10000, 100);
        M5.delay(1000);
        M5.Speaker.tone(4000, 20);
        M5.delay(1000);
    }
}
