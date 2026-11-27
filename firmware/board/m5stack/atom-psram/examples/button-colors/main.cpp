// SPDX-License-Identifier: MIT
// Adapted from M5Atom Basics/Button, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cinttypes>
#include <cstdio>

namespace {
constexpr gpio_num_t button_pin = GPIO_NUM_39;
constexpr gpio_num_t pixel_pin = GPIO_NUM_27;
constexpr uint32_t boot_color = 0xFF0000;
constexpr std::array<uint32_t, 4> colors{0x400000, 0x004000, 0x000040, 0x202020};

void show_color(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, uint32_t rgb)
{
    const std::array<uint8_t, 3> grb{
        static_cast<uint8_t>(rgb >> 8),
        static_cast<uint8_t>(rgb >> 16),
        static_cast<uint8_t>(rgb),
    };
    std::array<rmt_symbol_word_t, 25> waveform{};
    size_t index = 0;
    for (uint8_t value : grb) {
        for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
            auto& symbol = waveform[index++];
            const bool one = (value & mask) != 0;
            symbol.level0 = 1;
            symbol.duration0 = one ? 7 : 3;
            symbol.duration1 = one ? 6 : 9;
        }
    }
    // 10 MHz RMT ticks. The final 80 us low period latches the pixel.
    waveform.back().duration0 = 400;
    waveform.back().duration1 = 400;
    const rmt_transmit_config_t transmit{};
    ESP_ERROR_CHECK(rmt_transmit(channel, encoder, waveform.data(), sizeof(waveform), &transmit));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, 100));
}
} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    // GPIO39 is input-only. The board provides the pull-up.
    const gpio_config_t button_config{
        .pin_bit_mask = 1ULL << button_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
    rmt_tx_channel_config_t channel_config{};
    channel_config.gpio_num = pixel_pin;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = 10000000;
    channel_config.mem_block_symbols = 64;
    channel_config.trans_queue_depth = 1;
    rmt_channel_handle_t channel;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channel_config, &channel));
    const rmt_copy_encoder_config_t encoder_config{};
    rmt_encoder_handle_t encoder;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));

    std::puts("M5Atom Button colors: GPIO39 button, GPIO27 pixel 0");
    // Round up one tick so the initial delay lasts at least 10 ms.
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    show_color(channel, encoder, boot_color);
    std::printf("Button colors boot: RGB #%06" PRIX32 "\n", boot_color);

    size_t next_color = 0;
    bool pressed = false;
    bool candidate = false;
    TickType_t changed_at = xTaskGetTickCount();
    for (;;) {
        const bool sampled = gpio_get_level(button_pin) == 0;
        const TickType_t now = xTaskGetTickCount();
        if (sampled != candidate) {
            candidate = sampled;
            changed_at = now;
        }
        if (candidate != pressed && now - changed_at >= pdMS_TO_TICKS(20)) {
            pressed = candidate;
            if (pressed) {
                show_color(channel, encoder, colors[next_color]);
                std::printf("Button colors press: FSM=%u RGB #%06" PRIX32 "\n",
                            static_cast<unsigned>(next_color), colors[next_color]);
                next_color = (next_color + 1) % colors.size();
            }
        }
        vTaskDelay(1);
    }
}
