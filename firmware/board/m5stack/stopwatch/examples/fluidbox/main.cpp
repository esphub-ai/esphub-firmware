// EspHub entrypoint for the M5Stack factory FluidBox solver and renderer.
#include "../common/round_display.hpp"
#include "FluidBox.h"
#include "eh_agent.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <new>

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    void *memory = heap_caps_malloc(sizeof(FluidBox), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        centered("FluidBox needs PSRAM", 230, 2);
        return;
    }
    auto *fluid = new (memory) FluidBox;
    if (!fluid->begin(M5.Display.width(), M5.Display.height())) {
        centered("FluidBox init failed", 230, 2);
        return;
    }

    int64_t last_step = esp_timer_get_time();
    int64_t last_frame = last_step;
    int64_t last_sample = last_step;
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (M5.Imu.update()) {
            const auto &imu = M5.Imu.getImuData();
            fluid->setImuSample(imu.accel.x, imu.accel.y, imu.accel.z,
                                imu.gyro.x, imu.gyro.y, imu.gyro.z, (now - last_sample) * 1e-6f);
            last_sample = now;
        }
        const auto touch = M5.Touch.getDetail();
        if (touch.wasPressed()) fluid->applyTouchImpulse(touch.x, touch.y);
        if (M5.BtnA.wasPressed()) fluid->reset();
        if (now - last_step >= 1000000 / fluidbox_config::kSimulationHz) {
            fluid->step((now - last_step) * 1e-6f);
            last_step = now;
        }
        if (now - last_frame >= 1000000 / fluidbox_config::kRenderHz) {
            M5.Display.startWrite();
            fluid->render(M5.Display);
            M5.Display.endWrite();
            last_frame = now;
        }
        vTaskDelay(1);
    }
}
