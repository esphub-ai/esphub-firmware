// SPDX-FileCopyrightText: 2024 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/twai.h"
#include <M5Unified.h>
#include <cinttypes>
#include <cstdio>

namespace {
constexpr uint32_t alerts = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                            TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR;
constexpr auto interval = pdMS_TO_TICKS(1000);

void showStatus(const char* label, uint16_t color, const twai_status_info_t& status)
{
    auto& lcd = M5.Display;
    lcd.fillRect(0, 23, lcd.width(), lcd.height() - 23, TFT_BLACK);
    lcd.setCursor(4, 26);
    lcd.setTextColor(color);
    lcd.println(label);
    lcd.setTextColor(TFT_WHITE);
    lcd.printf("Bus errors: %" PRIu32 "\nTX queued: %" PRIu32
               "\nTX error: %" PRIu32 "\nTX failed: %" PRIu32 "\n",
               status.bus_error_count, status.msgs_to_tx,
               status.tx_error_counter, status.tx_failed_count);
}

bool check(esp_err_t result, const char* message, const char* failure)
{
    if (result == ESP_OK) {
        std::puts(message);
        return true;
    }
    std::printf("%s: %s\n", failure, esp_err_to_name(result));
    showStatus(failure, TFT_RED, {});
    return false;
}

void report(uint32_t flags)
{
    twai_status_info_t status{};
    if (twai_get_status_info(&status) != ESP_OK) return;

    if (flags & TWAI_ALERT_ERR_PASS) {
        std::puts("Alert: TWAI controller has become error passive.");
    }
    if (flags & TWAI_ALERT_BUS_ERROR) {
        std::puts("Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus.");
        std::printf("Bus error count: %" PRIu32 "\n", status.bus_error_count);
    }
    if (flags & TWAI_ALERT_TX_FAILED) {
        std::puts("Alert: The Transmission failed.");
        std::printf("TX buffered: %" PRIu32 "\tTX error: %" PRIu32 "\tTX failed: %" PRIu32 "\n",
                    status.msgs_to_tx, status.tx_error_counter, status.tx_failed_count);
    }
    if (flags & TWAI_ALERT_TX_SUCCESS) {
        std::puts("Alert: The Transmission was successful.");
        std::printf("TX buffered: %" PRIu32 "\n", status.msgs_to_tx);
    }

    if (flags & TWAI_ALERT_ERR_PASS) showStatus("Error passive", TFT_RED, status);
    else if (flags & TWAI_ALERT_TX_FAILED) showStatus("TX failed", TFT_RED, status);
    else if (flags & TWAI_ALERT_BUS_ERROR) showStatus("Bus error", TFT_RED, status);
    else if (flags & TWAI_ALERT_TX_SUCCESS) showStatus("TX success", TFT_GREEN, status);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& lcd = M5.Display;
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setFont(&fonts::Font2);
    lcd.fillRect(0, 0, lcd.width(), 21, TFT_BLUE);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("CAN 500 kbit/s", 4, 2);

    const twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_6, TWAI_MODE_NORMAL);
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (!check(twai_driver_install(&config, &timing, &filter), "Driver installed", "Install failed")) return;
    if (!check(twai_start(), "Driver started", "Start failed")) {
        twai_driver_uninstall();
        return;
    }
    if (!check(twai_reconfigure_alerts(alerts, nullptr), "CAN Alerts reconfigured", "Alerts failed")) {
        twai_stop();
        twai_driver_uninstall();
        return;
    }

    showStatus("Waiting for TX", TFT_WHITE, {});
    std::puts("CAN ready 500 kbit/s");
    twai_message_t frame{};
    frame.identifier = 0x0f6;
    frame.data_length_code = 4;
    auto previous = xTaskGetTickCount();
    for (;;) {
        // Alert polling wakes on either an interrupt or the next send deadline.
        const auto elapsed = xTaskGetTickCount() - previous;
        uint32_t triggered = 0;
        if (twai_read_alerts(&triggered, elapsed < interval ? interval - elapsed : 0) == ESP_OK) {
            report(triggered);
        }
        const auto now = xTaskGetTickCount();
        if (now - previous >= interval) {
            previous = now;
            if (twai_transmit(&frame, 0) == ESP_OK) {
                std::puts("Message queued for transmission");
            } else {
                std::puts("Failed to queue message for transmission");
            }
        }
    }
}
