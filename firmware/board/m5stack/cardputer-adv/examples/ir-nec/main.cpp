// Adapted for EspHub from https://github.com/m5stack/M5Cardputer/blob/bf64d3178ab43928fa02365fbe3937cec99716ad/examples/Basic/ir_nec/ir_nec.ino
#include <M5Unified.h>
#include "driver/rmt_tx.h"
#include "eh_agent.h"

static rmt_symbol_word_t pulse(uint16_t mark, uint16_t space)
{
    rmt_symbol_word_t symbol{};
    symbol.level0 = 1;
    symbol.duration0 = mark;
    symbol.duration1 = space;
    return symbol;
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    rmt_tx_channel_config_t config{};
    config.gpio_num = GPIO_NUM_44;
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 1000000; // one tick per microsecond
    config.mem_block_symbols = 64;
    config.trans_queue_depth = 1;
    rmt_channel_handle_t tx;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&config, &tx));
    rmt_carrier_config_t carrier{};
    carrier.frequency_hz = 38000;
    carrier.duty_cycle = 0.33f;
    ESP_ERROR_CHECK(rmt_apply_carrier(tx, &carrier));
    rmt_copy_encoder_config_t copy{};
    rmt_encoder_handle_t encoder;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy, &encoder));
    ESP_ERROR_CHECK(rmt_enable(tx));

    M5.Display.println("BtnA: send NEC IR");
    uint8_t command = 0x34;
    for (;;) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            const uint8_t bytes[] = {0x11, 0x11, command, static_cast<uint8_t>(~command)};
            rmt_symbol_word_t frame[34];
            size_t index = 0;
            frame[index++] = pulse(9000, 4500);
            for (uint8_t byte : bytes) {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    frame[index++] = pulse(560, (byte & (1u << bit)) ? 1690 : 560);
                }
            }
            frame[index] = pulse(560, 560);
            rmt_transmit_config_t send{};
            ESP_ERROR_CHECK(rmt_transmit(tx, encoder, frame, sizeof(frame), &send));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx, 1000));
            M5.Display.setCursor(0, 24);
            M5.Display.printf("Sent 0x1111 / 0x%02X", command++);
        }
        M5.delay(10);
    }
}
