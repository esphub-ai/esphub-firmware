// Adapted from the MIT-licensed M5AtomS3 Advanced MultiTask example.
// https://github.com/m5stack/M5AtomS3/blob/8accfe68d45ac8b31580e3067a0ecedfbf2d49c7/examples/Advanced/MultiTask/MultiTask.ino
#include "eh_agent.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <array>
#include <cinttypes>
#include <cstdio>

namespace {
struct TaskSpec {
    const char* name;
    uint32_t periodMs;
    UBaseType_t priority;
    BaseType_t coreId;
};

constexpr uint32_t taskStackBytes = 4096;
constexpr std::array<TaskSpec, 3> taskSpecs{{
    {"task1", 100, 1, 0},
    {"task2", 200, 2, 0},
    {"task3", 1000, 3, 0},
}};

void taskWorker(void* parameter)
{
    const auto& spec = *static_cast<const TaskSpec*>(parameter);
    std::printf("%s started: core=%d prio=%u period=%" PRIu32 "ms\n",
                spec.name,
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
                spec.periodMs);
    for (;;) {
        const auto uptimeMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        std::printf("%s Uptime (ms): %" PRIu64 "\n", spec.name, uptimeMs);
        vTaskDelay(pdMS_TO_TICKS(spec.periodMs));
    }
}

void showNotice(bool started)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(BLACK);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(4, 10);
    display.println("MultiTask");
    if (!started) {
        display.setCursor(4, 34);
        display.println("Task start failed");
        display.endWrite();
        return;
    }
    display.setCursor(4, 30);
    display.println("See serial port");
    display.setCursor(4, 42);
    display.println("for task details");
    display.setCursor(4, 66);
    display.println("task1: 100ms  c0 p1");
    display.setCursor(4, 80);
    display.println("task2: 200ms  c0 p2");
    display.setCursor(4, 94);
    display.println("task3: 1000ms c0 p3");
    display.endWrite();
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());

    std::array<TaskHandle_t, taskSpecs.size()> handles{};
    for (size_t i = 0; i < taskSpecs.size(); ++i) {
        const auto& spec = taskSpecs[i];
        const BaseType_t created = xTaskCreatePinnedToCore(
            taskWorker,
            spec.name,
            taskStackBytes,
            const_cast<TaskSpec*>(&spec),
            spec.priority,
            &handles[i],
            spec.coreId);
        if (created != pdPASS) {
            for (size_t j = 0; j < i; ++j) {
                vTaskDelete(handles[j]);
            }
            showNotice(false);
            std::printf("MultiTask: failed to create %s\n", spec.name);
            return;
        }
    }

    showNotice(true);
    std::puts("MultiTask: See serial port for task details");
}
