// SPDX-License-Identifier: MIT
// Adapted from M5Atom Advanced/MultSerial, copyright (c) 2021 M5Stack.
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "eh_agent.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdio>
#include <cstdarg>

namespace {
constexpr int grove_rx = 26;
constexpr int grove_tx = 32;
constexpr int baud_rate = 115200;
constexpr int buffer_size = 4096;

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

int discard_log(const char*, va_list)
{
    return 0;
}

void forward(uart_port_t from, uart_port_t to)
{
    std::array<uint8_t, 128> data;
    const int count = uart_read_bytes(from, data.data(), data.size(), 0);
    if (count > 0) {
        uart_write_bytes(to, data.data(), count);
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    start_uart(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    start_uart(UART_NUM_2, grove_tx, grove_rx);
    // Route console writes through the UART0 driver too, so it serializes them
    // with bridge writes. Bridge bytes bypass the console's newline handling.
    uart_vfs_dev_use_driver(UART_NUM_0);
    // UART0 carries raw application data after startup. Keep SDK services
    // running while preventing asynchronous log messages from corrupting it.
    esp_log_set_vprintf(discard_log);
    std::puts("M5Atom MultSerial ready: UART0 <-> UART2 RX=26 TX=32 115200 8N1");
    for (;;) {
        forward(UART_NUM_0, UART_NUM_2);
        forward(UART_NUM_2, UART_NUM_0);
        vTaskDelay(1);
    }
}
