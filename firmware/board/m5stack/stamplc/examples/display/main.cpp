// Adapted for EspHub from https://github.com/m5stack/M5StamPLC/blob/aebe5711205618ec65e885d6e2d93c5c751c23ef/examples/Basic/Display/Display.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdlib>
#include <M5Unified.h>

void setup()
{
    /* Init M5StamPLC */
    M5.begin();
}

void loop()
{
    /* Fill screen with colors */
    M5.Display.fillScreen(TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillScreen(TFT_RED);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillScreen(TFT_GREEN);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillScreen(TFT_BLUE);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillScreen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(800));

    /* Set cursor and print text */
    M5.Display.setCursor(10, 10);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.printf("Display Test!");
    vTaskDelay(pdMS_TO_TICKS(800));

    /* Draw shapes */
    M5.Display.drawRect(100, 50, 50, 50, BLUE);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillRect(100, 50, 50, 50, BLUE);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.drawCircle(100, 50, 50, RED);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillCircle(100, 50, 50, RED);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.drawTriangle(30, 30, 180, 50, 80, 100, YELLOW);
    vTaskDelay(pdMS_TO_TICKS(800));
    M5.Display.fillTriangle(30, 30, 180, 50, 80, 100, YELLOW);
    vTaskDelay(pdMS_TO_TICKS(800));

    /* Draw random triangles */
    for (int i = 0; i < 1000; i++) {
        M5.Display.fillTriangle(
            (rand() % (M5.Display.width() - 1)), (rand() % (M5.Display.height() - 1)),
            (rand() % (M5.Display.width() - 1)), (rand() % (M5.Display.height() - 1)),
            (rand() % (M5.Display.width() - 1)), (rand() % (M5.Display.height() - 1)), (rand() % 0xfffe));
    }
    vTaskDelay(pdMS_TO_TICKS(800));
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    setup();
    for (;;) {
        loop();
        vTaskDelay(1);
    }
}
