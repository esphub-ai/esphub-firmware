// SPDX-License-Identifier: MIT
// Adapted from M5Atom ATOM_StepMotor, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "eh_agent.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
constexpr auto button_pin = GPIO_NUM_39;
constexpr auto pixel_pin = GPIO_NUM_27;
constexpr auto enable_pin = GPIO_NUM_22;
constexpr auto direction_pin = GPIO_NUM_23;
constexpr auto step_pin = GPIO_NUM_19;
constexpr size_t pulse_count = 5000 * 32;

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

size_t encode_steps(const void*, size_t, size_t written, size_t capacity,
                    rmt_symbol_word_t* symbols, bool* done, void*)
{
    const size_t count = std::min(pulse_count - written, capacity);
    for (size_t i = 0; i < count; ++i) {
        rmt_symbol_word_t pulse{};
        pulse.level0 = 1;
        pulse.duration0 = 156;
        // Alternate 312 and 313 ticks at 10 MHz to average 32 kHz.
        pulse.duration1 = 156 + ((written + i) & 1);
        symbols[i] = pulse;
    }
    *done = written + count == pulse_count;
    return count;
}

void move(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, bool reverse)
{
    ESP_ERROR_CHECK(gpio_set_level(direction_pin, reverse));
    esp_rom_delay_us(2);
    const rmt_transmit_config_t config{};
    ESP_ERROR_CHECK(rmt_transmit(channel, encoder, &pulse_count, sizeof(pulse_count), &config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, 7000));
}
} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(gpio_set_level(enable_pin, 1));
    ESP_ERROR_CHECK(gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(direction_pin, 0));
    ESP_ERROR_CHECK(gpio_set_direction(direction_pin, GPIO_MODE_OUTPUT));
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

    auto motor_config = channel_config;
    motor_config.gpio_num = step_pin;
    motor_config.mem_block_symbols = 256;
    rmt_channel_handle_t motor;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&motor_config, &motor));
    rmt_simple_encoder_config_t step_encoder_config{};
    step_encoder_config.callback = encode_steps;
    step_encoder_config.min_chunk_size = 1;
    rmt_encoder_handle_t step_encoder;
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&step_encoder_config, &step_encoder));
    ESP_ERROR_CHECK(rmt_enable(motor));

    ESP_ERROR_CHECK(gpio_set_level(enable_pin, 0));
    show_color(channel, encoder, 0x00ff00);
    std::puts("Atom StepMotor enabled");
    vTaskDelay(pdMS_TO_TICKS(1600));
    std::puts("Atom StepMotor ready");
    bool pressed = false;
    bool last_sample = false;
    auto changed_at = xTaskGetTickCount();
    for (;;) {
        const bool sample = gpio_get_level(button_pin) == 0;
        const auto now = xTaskGetTickCount();
        if (sample != last_sample) {
            last_sample = sample;
            changed_at = now;
        }
        if (sample != pressed && now - changed_at >= pdMS_TO_TICKS(20)) {
            pressed = sample;
            if (pressed) {
                move(motor, step_encoder, false);
                move(motor, step_encoder, true);
                std::puts("Atom StepMotor done");
            }
        }
        vTaskDelay(1);
    }
}
