#pragma once

#include <M5Unified.h>

// Keep the labels inside the visible part of the round AMOLED.
inline void centered(const char *text, int y, int scale, uint16_t color = TFT_WHITE)
{
    auto &display = M5.Display;
    display.setTextDatum(lgfx::textdatum_t::middle_center);
    display.setTextColor(color, TFT_BLACK);
    display.setTextSize(scale);
    display.drawString(text, display.width() / 2, y);
}
