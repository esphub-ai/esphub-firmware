// ADV matrix wiring and key layout from M5Stack's M5Cardputer library:
// https://github.com/m5stack/M5Cardputer/tree/bf64d3178ab43928fa02365fbe3937cec99716ad/src/utility/Keyboard
#pragma once

#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <M5Unified.h>

namespace cardputer {

// Each row on the keycaps spans two halves of the TCA8418 matrix.
// Text, shifted text, USB HID usage, optional Fn-layer usage.
struct Key {
    char text, shifted;
    uint8_t usage, fn = 0;
};

inline constexpr Key layout[4][14] = {
    {{'`', '~', 53, 41}, {'1', '!', 30, 58}, {'2', '@', 31, 59}, {'3', '#', 32, 60}, {'4', '$', 33, 61},
     {'5', '%', 34, 62}, {'6', '^', 35, 63}, {'7', '&', 36, 64}, {'8', '*', 37, 65}, {'9', '(', 38, 66},
     {'0', ')', 39, 67}, {'-', '_', 45, 68}, {'=', '+', 46, 69}, {'\b', '\b', 42, 76}},
    {{'\t', '\t', 43}, {'q', 'Q', 20}, {'w', 'W', 26}, {'e', 'E', 8}, {'r', 'R', 21}, {'t', 'T', 23},
     {'y', 'Y', 28}, {'u', 'U', 24}, {'i', 'I', 12}, {'o', 'O', 18}, {'p', 'P', 19},
     {'[', '{', 47}, {']', '}', 48}, {'\\', '|', 49}},
    {{0, 0, 0}, {0, 0, 225}, {'a', 'A', 4}, {'s', 'S', 22}, {'d', 'D', 7}, {'f', 'F', 9}, {'g', 'G', 10},
     {'h', 'H', 11}, {'j', 'J', 13}, {'k', 'K', 14}, {'l', 'L', 15}, {';', ':', 51, 82},
     {'\'', '"', 52}, {'\n', '\n', 40}},
    {{0, 0, 224}, {0, 0, 227}, {0, 0, 226}, {'z', 'Z', 29}, {'x', 'X', 27}, {'c', 'C', 6}, {'v', 'V', 25},
     {'b', 'B', 5}, {'n', 'N', 17}, {'m', 'M', 16}, {',', '<', 54, 80}, {'.', '>', 55, 81},
     {'/', '?', 56, 79}, {' ', ' ', 44}}
};

struct KeyEvent {
    Key key{};
    bool down = false;
};

struct KeyboardReport {
    uint8_t modifiers = 0, reserved = 0;
    std::array<uint8_t, 6> keys{};
};
static_assert(sizeof(KeyboardReport) == 8);

class Keyboard {
public:
    bool begin()
    {
        // Seven row pins and eight column pins in keypad mode. Poll the FIFO
        // so the examples can also run without installing a GPIO ISR.
        if (!write(keypadRows, 0x7f) || !write(keypadColumns, 0xff) || !write(config, keyInterrupt | overflow)) {
            M5.Display.println("Keyboard unavailable");
            return false;
        }
        discardEvents();
        return true;
    }

    bool next(KeyEvent& event)
    {
        uint8_t status = 0;
        if (!read(interrupts, status)) return false;
        if (status & overflow) {
            // A lost release must not leave a key held on the host.
            discardEvents();
            held.clear();
            event = {};
            return true;
        }
        uint8_t code = 0;
        if (!read(fifo, code) || !(code & 0x7f)) return false;
        const int position = (code & 0x7f) - 1;
        const int matrixRow = position / 10, matrixCol = position % 10;
        if (matrixRow >= 7 || matrixCol >= 8) return false;
        const int row = matrixCol % 4, col = 2 * matrixRow + matrixCol / 4;
        event = {layout[row][col], (code & 0x80) != 0};
        auto prior = std::find_if(held.begin(), held.end(), [&](const Key& key) {
            return key.usage == event.key.usage;
        });
        if (event.down && prior == held.end()) held.push_back(event.key);
        if (!event.down && prior != held.end()) held.erase(prior);
        return true;
    }

    const std::vector<Key>& keys() const { return held; }
    bool hasUsage(uint8_t usage) const
    {
        return std::any_of(held.begin(), held.end(), [usage](const Key& key) { return key.usage == usage; });
    }
    bool shift() const { return hasUsage(225); }

    KeyboardReport report() const
    {
        KeyboardReport result{};
        size_t count = 0;
        const bool fn = hasUsage(0);
        for (const auto& key : held) {
            const uint8_t usage = fn && key.fn ? key.fn : key.usage;
            if (usage >= 224) {
                result.modifiers |= 1u << (usage - 224);
            } else if (usage != 0) {
                if (count < result.keys.size()) result.keys[count] = usage;
                ++count;
            }
        }
        if (count > result.keys.size()) result.keys.fill(1); // HID ErrorRollOver
        return result;
    }

private:
    static constexpr uint8_t config = 0x01, interrupts = 0x02, fifo = 0x04;
    static constexpr uint8_t keypadRows = 0x1d, keypadColumns = 0x1e;
    static constexpr uint8_t keyInterrupt = 0x01, overflow = 0x08;
    std::vector<Key> held;

    static bool read(uint8_t reg, uint8_t& data)
    {
        return M5.In_I2C.readRegister(0x34, reg, &data, 1, 400000);
    }
    static bool write(uint8_t reg, uint8_t data)
    {
        return M5.In_I2C.writeRegister8(0x34, reg, data, 400000);
    }
    static void discardEvents()
    {
        uint8_t unused;
        for (int i = 0; i < 10 && read(fifo, unused) && unused; ++i) {}
        write(interrupts, keyInterrupt | overflow);
    }
};

} // namespace cardputer
