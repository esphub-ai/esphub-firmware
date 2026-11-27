// SPDX-License-Identifier: MIT
// Adapted from M5Atom SCALES_KIT, copyright (c) 2022 M5Stack.
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
constexpr auto data_pin = GPIO_NUM_32;
constexpr auto clock_pin = GPIO_NUM_26;
constexpr float counts_per_gram = 27.61f;
portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;

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

bool read_sample(int32_t& sample)
{
    const auto started = xTaskGetTickCount();
    while (gpio_get_level(data_pin)) {
        if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(500)) {
            return false;
        }
        vTaskDelay(1);
    }

    uint32_t bits = 0;
    // Keep SCK high for less than the HX711 power-down threshold even
    // if the scheduler is busy. Readiness is polled outside this section.
    portENTER_CRITICAL(&clock_lock);
    for (unsigned pulse = 0; pulse < 25; ++pulse) {
        gpio_set_level(clock_pin, 1);
        esp_rom_delay_us(2);
        if (pulse < 24) {
            bits = (bits << 1) | gpio_get_level(data_pin);
        }
        gpio_set_level(clock_pin, 0);
        esp_rom_delay_us(2);
    }
    portEXIT_CRITICAL(&clock_lock);
    sample = static_cast<int32_t>(bits) - ((bits & 0x800000) ? 0x1000000 : 0);
    return true;
}

bool tare(int32_t& offset)
{
    int32_t total = 0;
    for (unsigned i = 0; i < 10; ++i) {
        int32_t sample;
        if (!read_sample(sample)) {
            return false;
        }
        total += sample;
    }
    offset = total / 10;
    return true;
}
} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    ESP_ERROR_CHECK(gpio_set_direction(data_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(button_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_level(clock_pin, 0));
    ESP_ERROR_CHECK(gpio_set_direction(clock_pin, GPIO_MODE_OUTPUT));
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

    std::puts("Calibration sensor....");
    show_color(channel, encoder, 0xff0000);
    int32_t offset = 0;
    while (!tare(offset)) {
        std::puts("Weight Unit not ready");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    show_color(channel, encoder, 0x00ff00);
    std::puts("Connect the Weight Unit to Port A (DOUT32/SCK26)");
    std::puts("Press Btn A to tare");
    bool pressed = false;
    for (;;) {
        const bool down = gpio_get_level(button_pin) == 0;
        if (down && !pressed) {
            show_color(channel, encoder, 0x000064);
            std::puts("Taring");
            if (!tare(offset)) {
                std::puts("Weight Unit not ready");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            show_color(channel, encoder, 0x00ff00);
        }
        pressed = down;
        int32_t raw;
        if (read_sample(raw)) {
            const float weight = std::max(0.0f, (raw - offset) / (counts_per_gram * 1000.0f));
            std::printf("Weight: %.2fkg\n", static_cast<double>(weight));
            const auto brightness = static_cast<uint32_t>(std::min(weight, 5.0f) * 20);
            show_color(channel, encoder, brightness << 8);
        } else {
            std::puts("Weight Unit not ready");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
