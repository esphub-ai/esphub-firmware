// Adapted for EspHub from https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Basics/camera/camera.ino
#include "eh_agent.h"
#include "camera_pins.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)POWER_GPIO_NUM, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)POWER_GPIO_NUM, 0));
    vTaskDelay(pdMS_TO_TICKS(500));
    camera_config_t config = {};
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;


    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 1;
    ESP_ERROR_CHECK(esp_camera_init(&config));
    for (;;) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame) {
            printf("Frame %ux%u: %u bytes\n", (unsigned)frame->width, (unsigned)frame->height, (unsigned)frame->len);
            esp_camera_fb_return(frame);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
