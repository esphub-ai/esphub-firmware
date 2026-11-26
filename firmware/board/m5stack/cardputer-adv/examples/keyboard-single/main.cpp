// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/keyboard/singlePress/singlePress.ino
#include "../common/keyboard.hpp"
#include "eh_agent.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    cardputer::Keyboard keyboard;
    if (!keyboard.begin()) return;
    M5.Display.setTextSize(2);
    M5.Display.println("Press A");
    constexpr uint8_t keyA = 4; // HID usage for the A key
    for (;;) {
        cardputer::KeyEvent event;
        while (keyboard.next(event)) {
            M5.Display.setCursor(0, 0);
            M5.Display.print(keyboard.hasUsage(keyA) ? "A pressed" : "Press A  ");
        }
        M5.delay(10);
    }
}
