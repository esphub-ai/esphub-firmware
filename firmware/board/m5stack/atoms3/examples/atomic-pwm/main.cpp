// Adapted from the MIT-licensed M5AtomS3 AtomicBase AtomicPWM example.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/AtomicBase/AtomicPWM/AtomicPWM.ino
#include "eh_agent.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cinttypes>
#include <cstdio>

namespace {
constexpr int signalPin = 5;
constexpr auto speedMode = LEDC_LOW_SPEED_MODE;
constexpr auto pwmTimer = LEDC_TIMER_0;
constexpr auto pwmChannel = LEDC_CHANNEL_0;
constexpr uint32_t pwmFreqHz = 10000;
constexpr auto pwmResolution = LEDC_TIMER_10_BIT;
constexpr uint32_t maxDuty = 500;
constexpr uint64_t stepPeriodUs = 2000;

void notifyStepTask(void* arg)
{
    xTaskNotifyGive(static_cast<TaskHandle_t>(arg));
}

void drawPwmScreen()
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::Orbitron_Light_24);
    display.setTextSize(1);
    display.drawString("PWM", display.width() / 2, display.height() / 2);
    display.endWrite();
}

void configurePwm()
{
    ledc_timer_config_t timerConfig{};
    timerConfig.speed_mode = speedMode;
    timerConfig.duty_resolution = pwmResolution;
    timerConfig.timer_num = pwmTimer;
    timerConfig.freq_hz = pwmFreqHz;
    timerConfig.clk_cfg = LEDC_USE_APB_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timerConfig));

    ledc_channel_config_t channelConfig{};
    channelConfig.gpio_num = signalPin;
    channelConfig.speed_mode = speedMode;
    channelConfig.channel = pwmChannel;
    channelConfig.intr_type = LEDC_INTR_DISABLE;
    channelConfig.timer_sel = pwmTimer;
    channelConfig.duty = 0;
    channelConfig.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channelConfig));
}

void setDuty(uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(speedMode, pwmChannel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(speedMode, pwmChannel));
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    drawPwmScreen();
    configurePwm();

    std::printf("AtomicPWM: GPIO%d LEDC ch%d %" PRIu32 "Hz 10-bit ramp 0..%" PRIu32 "..1 (2ms/step)\n",
                signalPin,
                static_cast<int>(pwmChannel),
                pwmFreqHz,
                maxDuty);

    esp_timer_handle_t stepTimer = nullptr;
    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = &notifyStepTask;
    timerArgs.arg = xTaskGetCurrentTaskHandle();
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "atomic_pwm_step";
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &stepTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(stepTimer, stepPeriodUs));

    uint32_t cycle = 0;
    for (;;) {
        ++cycle;
        for (uint32_t duty = 0; duty < maxDuty; ++duty) {
            setDuty(duty);
            if (duty == 0) {
                std::printf("AtomicPWM: cycle=%" PRIu32 " phase=up duty=0\n", cycle);
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        for (uint32_t duty = maxDuty; duty > 0; --duty) {
            setDuty(duty);
            if (duty == maxDuty) {
                std::printf("AtomicPWM: cycle=%" PRIu32 " phase=down duty=%" PRIu32 "\n",
                            cycle, maxDuty);
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}
