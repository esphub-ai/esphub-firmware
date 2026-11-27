// SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/twai.h"
#include "esp_log.h"
#include <M5Unified.h>
#include <cinttypes>
#include <cstdio>

namespace {
constexpr auto labelBackground = 0x222222u;
constexpr auto statusDuration = pdMS_TO_TICKS(1000);

void label(int y, const char* status)
{
    auto& display = M5.Display;
    display.fillRect(0, y, display.width(), 26, labelBackground);
    display.setTextDatum(top_left);
    display.setTextColor(TFT_WHITE, labelBackground);
    display.setTextSize(0.9f, 1.0f);
    display.drawString(status, 1, y);
    ESP_LOGI("can-base", "%s", status);
}

void printFrame(const twai_message_t& message)
{
    // Match the fields of the upstream recv(0) tuple.
    std::printf("(%" PRIu32 ", %s, %s, 255, b'", message.identifier,
                message.extd ? "True" : "False", message.rtr ? "True" : "False");
    for (unsigned i = 0; !message.rtr && i < message.data_length_code && i < sizeof(message.data); ++i) {
        const auto byte = message.data[i];
        if (byte == '\'' || byte == '\\') std::printf("\\%c", byte);
        else if (byte >= 32 && byte <= 126) std::putchar(byte);
        else std::printf("\\x%02x", unsigned(byte));
    }
    std::puts("')");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(labelBackground);
    display.fillRect(0, 0, display.width(), 30, TFT_BLUE);
    display.setFont(&fonts::DejaVu18);
    display.setTextDatum(top_left);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.drawString("CAN Base", 3, 3);
    label(38, "TX:");
    label(68, "RX:");

    const twai_general_config_t config =
        TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_6, TWAI_MODE_NORMAL);
    // Match the upstream 40 MHz XTAL table entry rather than the IDF timing preset.
    twai_timing_config_t timing{};
    timing.clk_src = TWAI_CLK_SRC_DEFAULT;
    timing.quanta_resolution_hz = 20000000;
    timing.tseg_1 = 15;
    timing.tseg_2 = 4;
    timing.sjw = 3;
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&config, &timing, &filter));
    ESP_ERROR_CHECK(twai_start());
    std::puts("TWAI started");

    twai_message_t outgoing{};
    outgoing.data_length_code = 7;
    const char payload[] = "uiflow2";
    for (unsigned i = 0; i < outgoing.data_length_code; ++i) outgoing.data[i] = payload[i];

    for (;;) {
        M5.update();
        if (M5.BtnA.isPressed()) {
            label(38, "TX: Send");
            const auto result = twai_transmit(&outgoing, 0);
            if (result != ESP_OK) std::printf("CAN send failed: %s\n", esp_err_to_name(result));
            vTaskDelay(statusDuration);
            label(38, "TX: Not Send");
        }

        twai_message_t incoming{};
        if (twai_receive(&incoming, 0) == ESP_OK) {
            label(68, "RX: Rec");
            printFrame(incoming);
            vTaskDelay(statusDuration);
            label(68, "RX: Not Rec");
        }
        vTaskDelay(1);
    }
}
