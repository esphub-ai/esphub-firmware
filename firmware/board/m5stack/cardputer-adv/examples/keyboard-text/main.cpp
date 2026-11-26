// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/keyboard/inputText/inputText.ino
#include <string>
#include "../common/keyboard.hpp"
#include "eh_agent.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    cardputer::Keyboard keyboard;
    if (!keyboard.begin()) return;
    std::string line, submitted;
    M5.Display.println("Type a line and press Enter");
    for (;;) {
        cardputer::KeyEvent event;
        while (keyboard.next(event)) {
            if (!event.down) continue;
            char c = keyboard.shift() ? event.key.shifted : event.key.text;
            if (c == '\n') {
                submitted = line;
                line.clear();
            } else if (c == '\b') {
                if (!line.empty()) line.pop_back();
            } else if (c >= ' ' && line.size() < 120) {
                line += c;
            }
            M5.Display.clear();
            M5.Display.setCursor(0, 0);
            M5.Display.printf("%s\n> %s", submitted.c_str(), line.c_str());
        }
        M5.delay(10);
    }
}
