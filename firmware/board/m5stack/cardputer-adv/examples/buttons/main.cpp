// Adapted from M5Stack's M5Cardputer Basic/button example by SeanKwok.
#include <M5Unified.h>
#include "eh_agent.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void show_state(const char *state, uint16_t color, unsigned presses)
{
    auto &lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_WHITE);
    lcd.setCursor(8, 8);
    lcd.print("G0 Button Test");
    lcd.fillRoundRect(8, 40, lcd.width() - 16, 50, 5, color);
    lcd.setTextColor(TFT_BLACK);
    lcd.setCursor(20, 57);
    lcd.print(state);
    lcd.setTextColor(TFT_WHITE);
    lcd.setCursor(8, 108);
    lcd.printf("Presses: %u", presses);
    ESP_LOGI("buttons", "%s (presses: %u)", state, presses);
}

extern "C" void app_main(void)
{
    M5.begin();
    M5.Display.setRotation(0);
    M5.Display.setBrightness(160);
    unsigned presses = 0;
    show_state("Press G0", TFT_CYAN, presses);
    ESP_ERROR_CHECK(eh_agent_start());
    while (true) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            show_state("Pressed", TFT_YELLOW, ++presses);
        } else if (M5.BtnA.wasHold()) {
            show_state("Held", TFT_ORANGE, presses);
        } else if (M5.BtnA.wasReleased()) {
            show_state("Released", TFT_GREEN, presses);
        }
        vTaskDelay(1);
    }
}
