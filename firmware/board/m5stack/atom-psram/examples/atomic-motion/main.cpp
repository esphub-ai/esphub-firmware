// SPDX-License-Identifier: MIT
// Adapted from M5Atom ATOM_Motion, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rmt_tx.h"
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdio>

namespace {
constexpr gpio_num_t button_pin = GPIO_NUM_39;
constexpr gpio_num_t pixel_pin = GPIO_NUM_27;
constexpr int transaction_timeout_ms = 40;
constexpr uint8_t motor_register = 0x20;

struct Motion {
    i2c_master_dev_handle_t device{};
    rmt_channel_handle_t pixel{};
    rmt_encoder_handle_t encoder{};
    std::atomic<bool> forward{true};
};

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

bool write_register(i2c_master_dev_handle_t device, uint8_t address, uint8_t value)
{
    const uint8_t data[]{address, value};
    const auto result = i2c_master_transmit(device, data, sizeof(data), transaction_timeout_ms);
    if (result != ESP_OK) {
        std::printf("Atom Motion I2C write 0x%02X: %s\n", address, esp_err_to_name(result));
    }
    return result == ESP_OK;
}

void print_register(i2c_master_dev_handle_t device, uint8_t address, const char* kind, int channel)
{
    uint8_t data = 0;
    const auto result = i2c_master_transmit_receive(device, &address, 1, &data, 1, transaction_timeout_ms);
    if (result != ESP_OK) {
        std::printf("Atom Motion I2C read 0x%02X: %s\n", address, esp_err_to_name(result));
        return;
    }
    const int value = address >= motor_register ? static_cast<int8_t>(data) : data;
    std::printf("%s Channel %d: %d\n", kind, channel, value);
}

void set_servos_and_report(i2c_master_dev_handle_t device, uint8_t angle)
{
    for (uint8_t reg = 0; reg < 4; ++reg) {
        if (!write_register(device, reg, angle)) {
            return;
        }
    }
    for (uint8_t reg = 0; reg < 4; ++reg) {
        print_register(device, reg, "Servo", reg + 1);
    }
    for (uint8_t index = 0; index < 2; ++index) {
        print_register(device, motor_register + index, "Motor", index + 1);
    }
}

void motion_task(void* argument)
{
    auto& motion = *static_cast<Motion*>(argument);
    for (;;) {
        set_servos_and_report(motion.device, 180);
        vTaskDelay(pdMS_TO_TICKS(1000));
        set_servos_and_report(motion.device, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        const bool forward = motion.forward.load(std::memory_order_relaxed);
        const auto speed = static_cast<uint8_t>(forward ? 100 : -100);
        if (write_register(motion.device, motor_register, speed) &&
            write_register(motion.device, motor_register + 1, speed)) {
            show_color(motion.pixel, motion.encoder, forward ? 0xff0000 : 0x0000ff);
        }
    }
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

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = GPIO_NUM_25;
    bus_config.scl_io_num = GPIO_NUM_21;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = 0x38;
    device_config.scl_speed_hz = 100000;

    static Motion motion;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device_config, &motion.device));
    motion.pixel = channel;
    motion.encoder = encoder;
    show_color(channel, encoder, 0);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(motion_task, "TaskMotion", 4096, &motion, 2, nullptr, 0)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    bool last_sample = false;
    bool pressed = false;
    TickType_t changed_at = xTaskGetTickCount();
    for (;;) {
        const bool sample = gpio_get_level(button_pin) == 0;
        const auto now = xTaskGetTickCount();
        if (sample != last_sample) {
            changed_at = now;
            last_sample = sample;
        }
        if (sample != pressed && now - changed_at >= pdMS_TO_TICKS(20)) {
            pressed = sample;
            if (pressed) {
                const bool forward = !motion.forward.load(std::memory_order_relaxed);
                motion.forward.store(forward, std::memory_order_relaxed);
                std::printf("Motor direction: %s\n", forward ? "forward" : "reverse");
            }
        }
        vTaskDelay(1);
    }
}
