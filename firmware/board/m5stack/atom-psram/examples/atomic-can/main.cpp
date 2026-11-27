// SPDX-License-Identifier: MIT
// Adapted from M5Atom ATOM_CAN, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/twai.h"
#include "eh_agent.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstdio>

#ifndef ATOMIC_CAN_SENDER
#define ATOMIC_CAN_SENDER 1
#endif

namespace {
constexpr gpio_num_t button_pin = GPIO_NUM_39;
constexpr gpio_num_t pixel_pin = GPIO_NUM_27;
constexpr uint32_t frame_id = 0x001;
constexpr bool sender = ATOMIC_CAN_SENDER != 0;

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

void send_frame(uint8_t type, uint8_t counter)
{
    twai_message_t frame{};
    frame.identifier = frame_id;
    frame.data_length_code = 8;
    frame.data[0] = type;
    frame.data[1] = counter;
    const esp_err_t result = twai_transmit(&frame, 0);
    if (result != ESP_OK) {
        std::printf("CAN transmit: %s\n", esp_err_to_name(result));
    }
}

void print_frame(const twai_message_t& frame)
{
    std::printf("New %s frame%s from 0x%08lX, DLC %u",
                frame.extd ? "extended" : "standard", frame.rtr ? " RTR" : "",
                static_cast<unsigned long>(frame.identifier), frame.data_length_code);
    if (!frame.rtr) {
        std::printf(", Data ");
        for (unsigned i = 0; i < std::min<unsigned>(frame.data_length_code, sizeof(frame.data)); ++i) {
            std::printf("0x%02X ", frame.data[i]);
        }
    }
    std::puts("");
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

    std::puts("Atom Install OK....");
    show_color(channel, encoder, 0x006400);

    // A103 replaces the K115 HDMI/FPGA base. CAN_TX and CAN_RX face the host.
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_22, GPIO_NUM_19, TWAI_MODE_NORMAL);
    general.rx_queue_len = 10;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&general, &timing, &filter));
    ESP_ERROR_CHECK(twai_start());
    std::puts("CAN Install");
    show_color(channel, encoder, 0x640000);

    constexpr TickType_t interval = pdMS_TO_TICKS(80);
    TickType_t last_send = xTaskGetTickCount();
    uint8_t counter = 0;
    for (;;) {
        if (gpio_get_level(button_pin) == 0) {
            show_color(channel, encoder, 0);
            esp_restart();
        }

        const TickType_t now = xTaskGetTickCount();
        if (sender && now - last_send >= interval) {
            last_send = now;
            send_frame(0x01, counter);
            counter = (counter + 1) % 30;
        }

        twai_message_t frame{};
        if (twai_receive(&frame, 1) != ESP_OK) {
            continue;
        }
        print_frame(frame);
        if (frame.rtr || frame.data_length_code < 2) {
            continue;
        }
        if (!sender && frame.data[0] == 0x01) {
            send_frame(0x02, frame.data[1]);
        } else if (sender && frame.data[0] == 0x02) {
            if (frame.data[1] < 25) {
                show_color(channel, encoder, 0x640000);
            } else if (frame.data[1] > 27) {
                show_color(channel, encoder, 0x000014);
            }
        }
    }
}
