// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicHDriver example.
#include "eh_agent.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr auto speedMode = LEDC_LOW_SPEED_MODE;
constexpr auto pwmTimer = LEDC_TIMER_0;
constexpr auto forwardChannel = LEDC_CHANNEL_0;
constexpr auto reverseChannel = LEDC_CHANNEL_1;
constexpr int forwardPin = 6;
constexpr int reversePin = 7;
constexpr int voltagePin = 8;
constexpr auto faultPin = GPIO_NUM_5;
constexpr uint32_t driveDuty = 1000;
constexpr uint32_t stopHoldMs = 2000;
constexpr int64_t reportIntervalUs = 250000;

void configurePwm()
{
    ledc_timer_config_t timer{};
    timer.speed_mode = speedMode;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = pwmTimer;
    timer.freq_hz = 10000;
    timer.clk_cfg = LEDC_USE_APB_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel{};
    channel.speed_mode = speedMode;
    channel.timer_sel = pwmTimer;
    channel.gpio_num = forwardPin;
    channel.channel = forwardChannel;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    channel.gpio_num = reversePin;
    channel.channel = reverseChannel;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void setDuty(ledc_channel_t channel, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(speedMode, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(speedMode, channel));
}

void drawScreen()
{
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::Orbitron_Light_24);
    display.setTextSize(1);
    display.drawString("H-Driver", display.width() / 2, display.height() / 2);
}
}  // namespace

extern "C" void app_main()
{
    configurePwm();
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawScreen();
    ESP_ERROR_CHECK(gpio_set_direction(faultPin, GPIO_MODE_INPUT));

    adc_unit_t unit;
    adc_channel_t channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(voltagePin, &unit, &channel));
    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = unit;
    adc_oneshot_unit_handle_t adc;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitConfig, &adc));
    const adc_oneshot_chan_cfg_t channelConfig{
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, channel, &channelConfig));

    bool forward = true;
    bool stoppedForHold = false;
    int64_t nextReportUs = 0;
    for (;;) {
        M5.update();
        if (!stoppedForHold &&
            (M5.BtnA.pressedFor(stopHoldMs) || M5.BtnA.wasReleaseFor(stopHoldMs))) {
            setDuty(forwardChannel, 0);
            setDuty(reverseChannel, 0);
            stoppedForHold = true;
        }
        if (M5.BtnA.wasReleased()) {
            if (!stoppedForHold) {
                // Clear the previously driven output before enabling the opposite one.
                setDuty(forward ? reverseChannel : forwardChannel, 0);
                setDuty(forward ? forwardChannel : reverseChannel, driveDuty);
                forward = !forward;
            }
            stoppedForHold = false;
        }

        const int64_t now = esp_timer_get_time();
        if (now >= nextReportUs) {
            nextReportUs = now + reportIntervalUs;
            int raw;
            const esp_err_t result = adc_oneshot_read(adc, channel, &raw);
            if (result == ESP_OK) {
                std::printf("VIN IN: %.2f\n", (raw * 10.0 / 4095.0) * 3.6);
            } else {
                std::printf("VIN IN: ADC read failed (%s)\n", esp_err_to_name(result));
            }
            if (gpio_get_level(faultPin) == 0) {
                std::puts("FAULT!");
            }
        }
        vTaskDelay(1);
    }
}
