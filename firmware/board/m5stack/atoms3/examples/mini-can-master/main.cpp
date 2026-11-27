// Adapted from the MIT-licensed M5AtomS3 Unit / MiniCAN_TJA1051T / Master example.
#include "eh_agent.h"
#include "driver/twai.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr unsigned commandId = 0x0b1;

void transmit(bool enabled)
{
    twai_message_t message{};
    message.identifier = commandId;
    message.data_length_code = 8;
    if (enabled) {
        for (unsigned i = 0; i < message.data_length_code; ++i) message.data[i] = i + 1;
    }
    const auto result = twai_transmit(&message, 0);
    if (result != ESP_OK) {
        std::printf("send cmd %s failed: %s\n", enabled ? "on" : "off", esp_err_to_name(result));
        return;
    }
    std::printf("send cmd %s to 0x%02X\nsend data:", enabled ? "on" : "off", commandId);
    for (const auto byte : message.data) std::printf(" 0x%02X", byte);
    std::putchar('\n');
}

void reportBus(uint32_t alerts)
{
    if (alerts & TWAI_ALERT_TX_SUCCESS) std::puts("CAN TX complete");
    if (alerts & TWAI_ALERT_BUS_ERROR) std::puts("CAN bus error");
    if (alerts & TWAI_ALERT_BUS_OFF) std::puts("CAN bus off");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(1);
    display.drawString("CAN Master", display.width() / 2, display.height() / 2);

    twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_2, GPIO_NUM_1, TWAI_MODE_NORMAL);
    config.alerts_enabled = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_25KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&config, &timing, &filter));
    std::puts("Driver installed");
    ESP_ERROR_CHECK(twai_start());
    std::puts("TWAI started");

    auto interval = xTaskGetTickCount();
    for (bool enabled = true;; enabled = !enabled) {
        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) == ESP_OK) reportBus(alerts);
        transmit(enabled);
        vTaskDelayUntil(&interval, pdMS_TO_TICKS(1000));
    }
}
