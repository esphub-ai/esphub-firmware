// Adapted from the MIT-licensed M5AtomS3 IMU Test v0.2.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Basics/imu/imu.ino
#include "eh_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

    auto& display = M5.Display;
    display.fillScreen(BLACK);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(8, 10);
    display.print("IMU: MPU6886");
    if (!M5.Imu.isEnabled()) {
        display.setCursor(8, 35);
        display.print("IMU unavailable");
        std::puts("IMU unavailable");
        return;
    }

    display.setCursor(8, 28);
    display.print("Accel (g)");
    display.setCursor(8, 75);
    display.print("Gyro (dps)");

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (M5.Imu.update()) {
            const auto& data = M5.Imu.getImuData();
            std::printf("IMU accel=%.2f %.2f %.2f gyro=%.2f %.2f %.2f\n",
                        data.accel.x, data.accel.y, data.accel.z,
                        data.gyro.x, data.gyro.y, data.gyro.z);

            display.startWrite();
            display.fillRect(0, 39, display.width(), 30, BLACK);
            display.fillRect(0, 86, display.width(), 30, BLACK);
            constexpr char axes[] = "XYZ";
            for (int axis = 0; axis < 3; ++axis) {
                display.setCursor(8, 39 + axis * 10);
                display.printf("%c  %.2f", axes[axis], data.accel.value[axis]);
                display.setCursor(8, 86 + axis * 10);
                display.printf("%c  %.2f", axes[axis], data.gyro.value[axis]);
            }
            display.endWrite();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}
