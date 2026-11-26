// https://docs.m5stack.com/en/arduino/m5air_quality/battery
#include "eh_agent.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

    // GPIO14 measures the battery through the board's 2:1 divider.
    adc_unit_t unit;
    adc_channel_t channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(14, &unit, &channel));
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = unit;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitConfig, &adc));
    const adc_oneshot_chan_cfg_t channelConfig{
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, channel, &channelConfig));

    auto& display = M5.Display;
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(3);

    for (;;) {
        M5.update();
        int raw = 0;
        const esp_err_t result = adc_oneshot_read(adc, channel, &raw);
        display.startWrite();
        display.fillScreen(TFT_BLACK);
        display.setCursor(10, 20);
        display.println("Battery:");
        if (result == ESP_OK) {
            const float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
            display.printf("%.2f V", voltage);
            std::printf("AirQ battery: GPIO14 raw=%d voltage=%.2f V at %lld us\n",
                        raw, voltage, static_cast<long long>(esp_timer_get_time()));
        } else {
            display.println("N/A");
            std::printf("AirQ battery: GPIO14 read failed (%s)\n", esp_err_to_name(result));
        }
        display.endWrite();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
