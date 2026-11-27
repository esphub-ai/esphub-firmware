// SPDX-License-Identifier: MIT
// Adapted from M5Atom Advanced/MultiTask, copyright (c) 2021 M5Stack.
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "eh_agent.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cinttypes>
#include <cstdio>

namespace {
struct TaskSpec {
    const char* name;
    uint32_t period_ms;
    UBaseType_t priority;
};

constexpr TaskSpec tasks[] = {
    {"task1", 100, 1},
    {"task2", 200, 2},
    {"task3", 1000, 3},
};
constexpr uint32_t stack_bytes = 4096;
constexpr int task_core = 0;
constexpr gpio_num_t pixel_pin = GPIO_NUM_27;

void report_uptime(void* argument)
{
    const auto& task = *static_cast<const TaskSpec*>(argument);
    std::printf("%s started: core=%d priority=%u stack=%" PRIu32
                " period_ms=%" PRIu32 "\n",
                task.name, static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
                stack_bytes, task.period_ms);

    TickType_t release = xTaskGetTickCount();
    for (;;) {
        std::printf("%s Uptime (ms): %" PRId64 "\n",
                    task.name, esp_timer_get_time() / 1000);
        xTaskDelayUntil(&release, pdMS_TO_TICKS(task.period_ms));
    }
}

esp_err_t set_orange_pixel()
{
    rmt_tx_channel_config_t channel_config{};
    channel_config.gpio_num = pixel_pin;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = 10000000;
    channel_config.mem_block_symbols = 64;
    channel_config.trans_queue_depth = 1;
    rmt_channel_handle_t channel;
    esp_err_t result = rmt_new_tx_channel(&channel_config, &channel);
    if (result != ESP_OK) {
        return result;
    }

    rmt_copy_encoder_config_t encoder_config{};
    rmt_encoder_handle_t encoder = nullptr;
    result = rmt_new_copy_encoder(&encoder_config, &encoder);
    if (result == ESP_OK) {
        result = rmt_enable(channel);
    }
    if (result == ESP_OK) {
        // The pixel takes MSB-first GRB. Durations are in 100 ns RMT ticks.
        constexpr std::array<uint8_t, 3> grb{0xA5, 0xFF, 0x00};
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
        // Hold the line low for 80 us to latch the static color.
        waveform.back().duration0 = 400;
        waveform.back().duration1 = 400;

        const rmt_transmit_config_t transmit{};
        result = rmt_transmit(channel, encoder, waveform.data(), sizeof(waveform), &transmit);
        if (result == ESP_OK) {
            result = rmt_tx_wait_all_done(channel, 100);
        }
        rmt_disable(channel);
    }
    if (encoder != nullptr) {
        rmt_del_encoder(encoder);
    }
    rmt_del_channel(channel);
    // Keep the data wire low after releasing the RMT channel.
    gpio_set_level(pixel_pin, 0);
    gpio_set_direction(pixel_pin, GPIO_MODE_OUTPUT);
    return result;
}
} // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    std::puts("M5Atom MultiTask");
    const esp_err_t pixel = set_orange_pixel();
    if (pixel == ESP_OK) {
        std::puts("RGB GPIO27: RMT TX complete, orange #FFA500");
    } else {
        std::printf("RGB GPIO27: RMT TX failed: %s\n", esp_err_to_name(pixel));
    }

    for (const auto& task : tasks) {
        if (xTaskCreatePinnedToCore(report_uptime, task.name, stack_bytes,
                                  const_cast<TaskSpec*>(&task), task.priority,
                                  nullptr, task_core) != pdPASS) {
            std::printf("MultiTask: could not start %s\n", task.name);
        }
    }
}
