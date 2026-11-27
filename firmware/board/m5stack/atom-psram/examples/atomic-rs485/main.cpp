// SPDX-License-Identifier: MIT
// Adapted from M5Atom ATOM_RS485, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "eh_agent.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace {
constexpr gpio_num_t button_pin = GPIO_NUM_39;
constexpr gpio_num_t pixel_pin = GPIO_NUM_27;
// Replace the K115 HDMI base with A131. These pins carry JTAG on the HDMI base.
constexpr int rs485_rx = 22;
constexpr int rs485_tx = 19;
constexpr int baud_rate = 115200;
constexpr int buffer_size = 2048;
constexpr TickType_t indication_ticks = pdMS_TO_TICKS(50);

int discard_log(const char*, va_list)
{
    return 0;
}

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
void start_uart(uart_port_t port, int tx, int rx)
{
    uart_config_t config{};
    config.baud_rate = baud_rate;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(port, &config));
    ESP_ERROR_CHECK(uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(port, buffer_size, 0, 0, nullptr, 0));
}

} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    start_uart(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    start_uart(UART_NUM_2, rs485_tx, rs485_rx);
    uart_vfs_dev_use_driver(UART_NUM_0);
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


    show_color(channel, encoder, 0);
    // UART0 carries unmodified received bytes once the example is ready.
    esp_log_set_vprintf(discard_log);
    std::puts("M5Atom Atomic RS485 ready: UART2 RX=22 TX=19 115200 8N1");

    bool pressed = false;
    bool candidate = false;
    TickType_t changed_at = xTaskGetTickCount();
    bool lit = false;
    TickType_t lit_at = 0;
    for (;;) {
        const bool sampled = gpio_get_level(button_pin) == 0;
        const TickType_t now = xTaskGetTickCount();
        uint32_t color = 0;
        if (sampled != candidate) {
            candidate = sampled;
            changed_at = now;
        }
        if (candidate != pressed && now - changed_at >= pdMS_TO_TICKS(20)) {
            pressed = candidate;
            if (pressed) {
                uart_write_bytes(UART_NUM_2, "a", 1);
                color = 0xFF00FF;
            }
        }

        // Keep receiving during the indicator interval. A simultaneous RX
        // indication is deferred one iteration so the TX color is observable.
        if (color == 0) {
            std::array<uint8_t, 128> bytes;
            const int count = uart_read_bytes(UART_NUM_2, bytes.data(), bytes.size(), 0);
            if (count > 0) {
                uart_write_bytes(UART_NUM_0, bytes.data(), count);
                color = 0x00FFFF;
            }
        }
        if (color != 0) {
            show_color(channel, encoder, color);
            lit = true;
            lit_at = xTaskGetTickCount();
        } else if (lit && now - lit_at >= indication_ticks) {
            show_color(channel, encoder, 0);
            lit = false;
        }
        vTaskDelay(1);
    }
}
