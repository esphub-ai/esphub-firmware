// https://docs.m5stack.com/en/arduino/m5air_quality/buzzer
#include "eh_agent.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr int buzzerPin = 9;
constexpr auto speedMode = LEDC_LOW_SPEED_MODE;
constexpr auto timer = LEDC_TIMER_0;
constexpr auto channel = LEDC_CHANNEL_0;
constexpr uint32_t halfDuty = 1u << 9;

struct Note {
    uint32_t hertz;
    uint32_t milliseconds;
};

constexpr Note melody[] = {
    {262, 250}, {262, 250}, {392, 250}, {392, 250},
    {440, 250}, {440, 250}, {392, 500},
    {349, 250}, {349, 250}, {330, 250}, {330, 250},
    {294, 250}, {294, 250}, {262, 500},
};
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.drawString("Playing:", 20, 20);
    display.drawString("Twinkle Twinkle", 20, 60);
    display.drawString("Little Star", 20, 100);
    display.endWrite();
    std::puts("Playing: Twinkle Twinkle Little Star");

    ledc_timer_config_t pwmTimer{};
    pwmTimer.speed_mode = speedMode;
    pwmTimer.duty_resolution = LEDC_TIMER_10_BIT;
    pwmTimer.timer_num = timer;
    pwmTimer.freq_hz = melody[0].hertz;
    pwmTimer.clk_cfg = LEDC_USE_APB_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&pwmTimer));

    ledc_channel_config_t pwmChannel{};
    pwmChannel.gpio_num = buzzerPin;
    pwmChannel.speed_mode = speedMode;
    pwmChannel.channel = channel;
    pwmChannel.timer_sel = timer;
    ESP_ERROR_CHECK(ledc_channel_config(&pwmChannel));

    for (;;) {
        for (const auto& note : melody) {
            ESP_ERROR_CHECK(ledc_set_freq(speedMode, timer, note.hertz));
            ESP_ERROR_CHECK(ledc_set_duty(speedMode, channel, halfDuty));
            ESP_ERROR_CHECK(ledc_update_duty(speedMode, channel));
            vTaskDelay(pdMS_TO_TICKS(note.milliseconds));
            ESP_ERROR_CHECK(ledc_stop(speedMode, channel, 0));
            // The Arduino sketch waits 120% of the tone duration.
            vTaskDelay(pdMS_TO_TICKS(note.milliseconds / 5));
            M5.update();
        }
    }
}
