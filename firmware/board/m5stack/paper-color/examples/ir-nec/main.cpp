// https://docs.m5stack.com/en/arduino/papercolor/ir_nec
#include "eh_agent.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
constexpr uint16_t address = 0x1111;
constexpr gpio_num_t irPin = GPIO_NUM_48;
constexpr int64_t second = 1000000;

rmt_symbol_word_t pulse(uint16_t mark, uint16_t space)
{
    rmt_symbol_word_t symbol{};
    symbol.level0 = 1;
    symbol.duration0 = mark;
    symbol.duration1 = space;
    return symbol;
}

void sendNec(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, uint8_t command)
{
    std::array<rmt_symbol_word_t, 34> frame{};
    frame.front() = pulse(9000, 4500);
    const uint32_t data = address | (uint32_t{command} << 16) | (uint32_t{uint8_t(command ^ 0xff)} << 24);
    for (unsigned bit = 0; bit < 32; ++bit) {
        frame[bit + 1] = pulse(560, ((data >> bit) & 1) ? 1690 : 560);
    }
    frame.back() = pulse(560, 0);
    rmt_transmit_config_t tx{};
    ESP_ERROR_CHECK(rmt_transmit(channel, encoder, frame.data(), sizeof(frame), &tx));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, 200));
}

void showStatus(uint8_t command)
{
    auto& display = M5.Display;
    const int x = display.width() / 2;
    const int y = display.height() / 2;
    char text[32];
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(BLACK);
    display.drawString("IR NEC SEND TEST", x, y - 70);
    std::snprintf(text, sizeof(text), "Address 0x%04X", unsigned(address));
    display.setTextColor(BLUE);
    display.drawString(text, x, y);
    std::snprintf(text, sizeof(text), "Command 0x%02X", unsigned(command));
    display.setTextColor(RED);
    display.drawString(text, x, y + 50);
    display.endWrite();
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    M5.Display.setRotation(1);

    rmt_tx_channel_config_t channelConfig{};
    channelConfig.gpio_num = irPin;
    channelConfig.clk_src = RMT_CLK_SRC_APB;
    channelConfig.resolution_hz = 1000000;
    channelConfig.mem_block_symbols = 48;
    channelConfig.trans_queue_depth = 1;
    rmt_channel_handle_t channel = nullptr;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channelConfig, &channel));

    rmt_carrier_config_t carrier{};
    carrier.frequency_hz = 38000;
    carrier.duty_cycle = 0.30f;
    ESP_ERROR_CHECK(rmt_apply_carrier(channel, &carrier));
    rmt_copy_encoder_config_t encoderConfig{};
    rmt_encoder_handle_t encoder = nullptr;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoderConfig, &encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));

    uint8_t command = 0x34;
    showStatus(command);
    const int64_t start = esp_timer_get_time();
    int64_t nextSend = start;
    uint32_t sent = 0;
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (now >= nextSend) {
            const auto current = static_cast<uint8_t>(0x34 + (now - start) / (60 * second));
            if (current != command) {
                command = current;
                showStatus(command);
            }
            sendNec(channel, encoder, command);
            std::printf("IR NEC sent: addr=0x%04X cmd=0x%02X repeats=0 count=%lu\n",
                        unsigned(address), unsigned(command), static_cast<unsigned long>(++sent));
            // Keep the cadence anchored to startup, even if refreshing the panel took time.
            nextSend = start + ((esp_timer_get_time() - start) / second + 1) * second;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
