// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/keyboard/multiPress/multiPress.ino
#include "../common/keyboard.hpp"
#include "eh_agent.h"

static const char* keyName(const cardputer::Key& key)
{
    switch (key.usage) {
    case 0:   return "Fn";
    case 224: return "Ctrl";
    case 225: return "Shift";
    case 226: return "Alt";
    case 227: return "Opt";
    case 40:  return "Enter";
    case 42:  return "Backspace";
    case 43:  return "Tab";
    case 44:  return "Space";
    default:  return nullptr;
    }
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    cardputer::Keyboard keyboard;
    if (!keyboard.begin()) return;
    M5.Display.setTextSize(2);
    M5.Display.println("Hold some keys");
    for (;;) {
        cardputer::KeyEvent event;
        while (keyboard.next(event)) {
            M5.Display.clear();
            M5.Display.setCursor(0, 0);
            bool first = true;
            for (const auto& key : keyboard.keys()) {
                if (!first) M5.Display.print("+");
                if (const char* name = keyName(key)) M5.Display.print(name);
                else M5.Display.write(keyboard.shift() ? key.shifted : key.text);
                first = false;
            }
            if (first) M5.Display.print("Hold some keys");
        }
        M5.delay(10);
    }
}
