// Adapted from the MIT-licensed M5AtomS3 Unit / MiniCAN_TJA1051T / Slave example.
#include "eh_agent.h"
#include "driver/twai.h"
#include <M5Unified.h>
#include <cinttypes>
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(1);
    display.drawString("CAN Slave", display.width() / 2, display.height() / 2);

    const twai_general_config_t config =
        TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_2, GPIO_NUM_1, TWAI_MODE_NORMAL);
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_25KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&config, &timing, &filter));
    std::puts("Driver installed");
    ESP_ERROR_CHECK(twai_start());
    std::puts("TWAI started");

    for (;;) {
        twai_message_t message{};
        const auto result = twai_receive(&message, portMAX_DELAY);
        if (result != ESP_OK) {
            std::printf("CAN receive failed: %s\n", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        std::printf("received identifier: 0x%02" PRIX32 "\nreceived data:", message.identifier);
        if (!message.rtr) {
            for (unsigned i = 0; i < message.data_length_code && i < sizeof(message.data); ++i) {
                std::printf(" 0x%02X", unsigned(message.data[i]));
            }
        }
        std::putchar('\n');
    }
}
