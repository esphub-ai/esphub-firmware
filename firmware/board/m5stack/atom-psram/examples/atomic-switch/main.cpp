// SPDX-License-Identifier: MIT
// Adapted from M5Atom ATOM_Switch, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdio>

namespace {
constexpr auto button_pin = GPIO_NUM_39;
constexpr auto pixel_pin = GPIO_NUM_27;
constexpr std::array relay_pins{GPIO_NUM_22, GPIO_NUM_19};
constexpr std::array<uint32_t, 2> cue_colors{0x00aaff, 0xaa00ff};
constexpr int steps = 128;

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

uint32_t scaled(uint32_t rgb, unsigned brightness)
{
    const auto scale = [brightness](uint32_t component) {
        return (component * (brightness + 1)) >> 8;
    };
    return (scale(rgb >> 16) << 16) |
           (scale((rgb >> 8) & 0xff) << 8) |
           scale(rgb & 0xff);
}
} // namespace

extern "C" void app_main()
{
    for (auto pin : relay_pins) {
        ESP_ERROR_CHECK(gpio_set_level(pin, 0));
        ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
    }
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

    std::array<bool, 2> on{};
    bool last_sample = false;
    bool pressed = false;
    auto changed_at = xTaskGetTickCount();
    auto tick = changed_at;
    std::puts("Atom Switch ready");
    for (unsigned frame = 0;; frame = (frame + 1) % (4 * steps)) {
        const auto phase = frame / steps;
        const auto index = phase / 2;
        const auto step = frame % steps;
        const bool interactive = phase % 2 != 0;
        const bool sample = gpio_get_level(button_pin) == 0;
        const auto now = xTaskGetTickCount();
        if (sample != last_sample) {
            last_sample = sample;
            changed_at = now;
        }
        if (sample != pressed && now - changed_at >= pdMS_TO_TICKS(20)) {
            pressed = sample;
            if (pressed && interactive) {
                on[index] = !on[index];
                ESP_ERROR_CHECK(gpio_set_level(relay_pins[index], on[index]));
                std::printf("switch%u %s\n", index + 1, on[index] ? "high" : "low");
            }
        }
        const uint32_t color = interactive ? (on[index] ? 0xff0000 : 0x00ff00) : cue_colors[index];
        show_color(channel, encoder, scaled(color, interactive ? step : steps - step));
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(20));
    }
}
