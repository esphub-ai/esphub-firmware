// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "eh_agent.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace {
void reportCallback(esp_log_level_t level, bool useColor, const char* message)
{
    // A separate UART diagnostic makes the callback and its suffix observable.
    // Write directly to stdout so the diagnostic cannot recurse through M5.Log.
    const size_t size = std::strlen(message);
    std::printf("Callback level=%d color=%d bytes=%zu text=\"", int(level), useColor, size);
    for (size_t i = 0; i < size; ++i) {
        if (message[i] == '\n') std::fputs("\\n", stdout);
        else std::putchar(message[i]);
    }
    std::puts("\"");
}

void configureLogTargets()
{
    M5.setLogDisplayIndex(0);
    M5.Display.setTextWrap(true, true);
    M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_VERBOSE);
    M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_DEBUG);
    M5.Log.setLogLevel(m5::log_target_callback, ESP_LOG_INFO);
    M5.Log.setCallback(reportCallback);
    M5.Log.setEnableColor(m5::log_target_serial, true);
    M5.Log.setEnableColor(m5::log_target_display, true);
    M5.Log.setEnableColor(m5::log_target_callback, true);
    M5.Log.setSuffix(m5::log_target_serial, "\n");
    M5.Log.setSuffix(m5::log_target_display, "\n");
    M5.Log.setSuffix(m5::log_target_callback, "");
}

void showStartupLogs()
{
    M5.Log(ESP_LOG_ERROR, "M5.Log error log");
    M5.Log(ESP_LOG_WARN, "M5.Log warn log");
    M5.Log(ESP_LOG_INFO, "M5.Log info log");
    M5.Log(ESP_LOG_DEBUG, "M5.Log debug log");
    M5.Log(ESP_LOG_VERBOSE, "M5.Log verbose log");
    M5_LOGE("M5_LOGE error log");
    M5_LOGW("M5_LOGW warn log");
    M5_LOGI("M5_LOGI info log");
    M5_LOGD("M5_LOGD debug log");
    M5_LOGV("M5_LOGV verbose log");
    M5.Log.printf("M5.Log.printf non level output\n");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());

    // Raise the runtime level only for the example's ESP-IDF tag.
    esp_log_level_set("TAG", ESP_LOG_VERBOSE);
    ESP_LOGE("TAG", "using ESP_LOGE error log.");
    ESP_LOGW("TAG", "using ESP_LOGW warn log.");
    ESP_LOGI("TAG", "using ESP_LOGI info log.");
    ESP_LOGD("TAG", "using ESP_LOGD debug log.");
    ESP_LOGV("TAG", "using ESP_LOGV verbose log.");

    M5.begin(M5.config());
    configureLogTargets();
    showStartupLogs();

    uint32_t counter = 0;
    int previousY = 0;
    for (;;) {
        M5.delay(1);
        M5.update();
        // AtomS3 has one physical button (BtnA).
        if (M5.BtnA.wasClicked()) M5_LOGW("BtnA %d click", M5.BtnA.getClickCount());
        if ((++counter & 0x3ff) == 0) {
            const int cursorY = M5.Display.getCursorY();
            if (previousY > cursorY) {
                M5.Display.clear();
                std::printf("LCD wrap cleared (y=%d -> %d)\n", previousY, cursorY);
            }
            previousY = cursorY;
            M5_LOGV("count:%u", unsigned(counter >> 10));
        }
    }
}
