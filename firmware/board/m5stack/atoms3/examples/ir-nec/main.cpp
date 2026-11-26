// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Basics/ir_nec/ir_nec.ino
#include "eh_agent.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
constexpr uint16_t address = 0x1111;
constexpr gpio_num_t irPin = GPIO_NUM_4;

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
    char text[16];
    display.startWrite();
    display.fillScreen(BLACK);
    display.setTextDatum(middle_center);
    display.setTextColor(GREEN);
    display.drawString("IR NEC SEND", x, y - 40);
    display.drawString("ADDR:0x1111", x, y);
    std::snprintf(text, sizeof(text), "CMD:0x%02X", unsigned(command));
    display.drawString(text, x, y + 40);
    display.fillCircle(10, 105, 8, GREEN);
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

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
    TickType_t nextSend = xTaskGetTickCount();
    for (;;) {
        showStatus(command);
        sendNec(channel, encoder, command);
        std::printf("IR NEC sent: addr=0x%04X cmd=0x%02X repeats=0\n",
                    unsigned(address), unsigned(command));
        ++command;
        vTaskDelayUntil(&nextSend, pdMS_TO_TICKS(500));
        M5.Display.fillCircle(10, 105, 8, YELLOW);
        vTaskDelayUntil(&nextSend, pdMS_TO_TICKS(500));
    }
}
