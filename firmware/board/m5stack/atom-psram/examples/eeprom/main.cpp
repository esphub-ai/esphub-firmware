// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <array>
#include <cstdio>

namespace {
using Bytes = std::array<uint8_t, 32>;
constexpr char storageNamespace[] = "atomps_eeprom";
constexpr char key[] = "bytes";
constexpr auto button = GPIO_NUM_39;

esp_err_t readBytes(Bytes& bytes)
{
    nvs_handle_t storage;
    auto status = nvs_open(storageNamespace, NVS_READONLY, &storage);
    if (status != ESP_OK) return status;

    size_t size = bytes.size();
    status = nvs_get_blob(storage, key, bytes.data(), &size);
    nvs_close(storage);
    return status == ESP_OK && size != bytes.size() ? ESP_ERR_INVALID_SIZE : status;
}

esp_err_t saveBytes(const Bytes& bytes)
{
    nvs_handle_t storage;
    auto status = nvs_open(storageNamespace, NVS_READWRITE, &storage);
    if (status != ESP_OK) return status;

    status = nvs_set_blob(storage, key, bytes.data(), bytes.size());
    if (status == ESP_OK) status = nvs_commit(storage);
    nvs_close(storage);
    return status;
}

void showBytes(const char* state, const Bytes& bytes)
{
    std::printf("EEPROM %s:", state);
    for (auto value : bytes) std::printf(" %u", unsigned(value));
    std::putchar('\n');
}

void writeAndReadBack()
{
    Bytes written;
    esp_fill_random(written.data(), written.size());
    const auto status = saveBytes(written);
    if (status != ESP_OK) {
        std::printf("EEPROM write failed: %s\n", esp_err_to_name(status));
        return;
    }
    showBytes("Committed", written);

    Bytes readback;
    const auto readStatus = readBytes(readback);
    if (readStatus != ESP_OK) {
        std::printf("EEPROM read failed: %s\n", esp_err_to_name(readStatus));
    } else if (written != readback) {
        std::puts("EEPROM readback mismatch");
    } else {
        showBytes("Read back", readback);
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    Bytes initial;
    const auto status = readBytes(initial);
    if (status == ESP_ERR_NVS_NOT_FOUND) initial.fill(0xff);
    if (status == ESP_OK || status == ESP_ERR_NVS_NOT_FOUND) {
        showBytes("Startup read", initial);
    } else {
        std::printf("EEPROM startup read failed: %s\n", esp_err_to_name(status));
    }

    // GPIO39 has a board pull-up and is active low.
    ESP_ERROR_CHECK(gpio_set_direction(button, GPIO_MODE_INPUT));
    std::puts("Press GPIO39 button to write EEPROM");
    bool previous = false;
    for (;;) {
        const bool pressed = gpio_get_level(button) == 0;
        if (pressed != previous) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (pressed == (gpio_get_level(button) == 0)) {
                previous = pressed;
                if (pressed) writeAndReadBack();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
