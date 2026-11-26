// M5Stack Cardputer-Adv IMU example: https://docs.m5stack.com/en/arduino/m5cardputer/imu
#include <M5Unified.h>
#include "eh_agent.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    if (!M5.Imu.isEnabled()) {
        M5.Display.println("BMI270 unavailable");
        return;
    }
    for (;;) {
        if (M5.Imu.update()) {
            const auto& data = M5.Imu.getImuData();
            M5.Display.setCursor(0, 0);
            M5.Display.printf("    Acc(g) Gyro(d/s)\n"
                              "X %7.2f %8.2f\nY %7.2f %8.2f\nZ %7.2f %8.2f\n",
                              data.accel.x, data.gyro.x, data.accel.y, data.gyro.y,
                              data.accel.z, data.gyro.z);
        }
        M5.delay(100);
    }
}
