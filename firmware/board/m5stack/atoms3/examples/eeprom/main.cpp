// Adapted from the MIT-licensed M5AtomS3 EEPROM example.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Advanced/Storage/EEPROM/EEPROM.ino
#include "eh_agent.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <M5Unified.h>
#include <array>
#include <cstdio>

namespace {
using Bytes = std::array<uint8_t, 16>;
constexpr char key[] = "bytes";

esp_err_t readBytes(nvs_handle_t storage, Bytes& bytes)
{
    size_t length = bytes.size();
    const auto result = nvs_get_blob(storage, key, bytes.data(), &length);
    return result == ESP_OK && length != bytes.size() ? ESP_ERR_INVALID_SIZE : result;
}

void showBytes(const char* state, const Bytes& bytes)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(BLACK);
    display.setCursor(8, 12);
    display.print("EEPROM offsets");
    display.setCursor(8, 31);
    display.print(state);
    std::printf("EEPROM %s:", state);
    for (unsigned i = 0; i < bytes.size(); ++i) {
        display.setCursor(8 + 28 * (i % 4), 49 + 12 * (i / 4));
        display.printf("%02X", unsigned(bytes[i]));
        std::printf(" %02X=%02X", i, unsigned(bytes[i]));
    }
    display.setCursor(8, 107);
    display.print("Press A to write");
    display.endWrite();
    std::puts("");
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    M5.Display.setTextColor(WHITE, BLACK);

    nvs_handle_t storage;
    ESP_ERROR_CHECK(nvs_open("atoms3_eeprom", NVS_READWRITE, &storage));
    Bytes bytes;
    bytes.fill(0xff);
    const auto status = readBytes(storage, bytes);
    if (status != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(status);
    }
    showBytes("Startup read", bytes);

    for (;;) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            Bytes written;
            esp_fill_random(written.data(), written.size());
            ESP_ERROR_CHECK(nvs_set_blob(storage, key, written.data(), written.size()));
            ESP_ERROR_CHECK(nvs_commit(storage));
            showBytes("Committed", written);
            ESP_ERROR_CHECK(readBytes(storage, bytes));
            showBytes("Read back", bytes);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
