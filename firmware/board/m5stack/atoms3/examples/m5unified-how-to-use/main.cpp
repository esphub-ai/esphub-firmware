// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include <M5Unified.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace {
auto& display = M5.Display;

void showBoard(const char* boardName, const char* imuName)
{
    display.setBrightness(128);
    display.startWrite();
    for (int y = 0; y < display.height(); ++y) {
        for (int x = 0; x < display.width(); ++x) {
            display.writePixel(x, y, display.color888(x * 2, x + y, y * 2));
        }
    }
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(0, 0);
    display.println("Display 0");
    display.printf("Core:%s\n", boardName);
    display.printf("IMU:%s\n", imuName);
    display.endWrite();
}

void updateButton(int h)
{
    const auto& button = M5.BtnA;
    const int state = button.wasHold() ? 1
                    : button.wasClicked() ? 2
                    : button.wasPressed() ? 3
                    : button.wasReleased() ? 4
                    : button.wasDecideClickCount() ? 5 : 0;
    if (!state) return;

    constexpr uint16_t colors[] = {TFT_BLACK, TFT_CYAN, TFT_RED, TFT_YELLOW, TFT_BLUE, TFT_GREEN};
    constexpr const char* names[] = {
        "", "wasHold", "wasClicked", "wasPressed", "wasReleased", "wasDecideClickCount"
    };
    const auto count = unsigned(button.getClickCount());
    M5_LOGI("BtnA:%s  count:%u", names[state], count);
    display.startWrite();
    display.fillRect(0, h * 3, h, h - 1, colors[state]);
    display.setCursor(0, h * 3);
    display.setTextColor(state == 4 ? TFT_WHITE : TFT_BLACK, colors[state]);
    display.printf("%u", count);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.endWrite();
}

void updateBattery()
{
    static uint32_t previousSec = M5.millis() / 1000;
    static int previousBattery = INT_MAX;
    const uint32_t sec = M5.millis() / 1000;
    if (sec == previousSec) return;
    previousSec = sec;
    const int battery = M5.Power.getBatteryLevel();
    std::printf("Battery poll: sec=%u value=%d\n", unsigned(sec), battery);
    if (battery == previousBattery) return;
    previousBattery = battery;
    display.setCursor(0, display.fontHeight() * 3);
    if (battery < 0) display.print("Bat:none");
    else display.printf("Bat:%03d", battery);
}

void updateImu(int h)
{
    if (!M5.Imu.isEnabled() || !M5.Imu.update()) return;
    const auto data = M5.Imu.getImuData();
    const int width = display.width();
    const int center = (width + h) >> 1;
    constexpr uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_RED, TFT_GREEN, TFT_BLUE};
    static std::array<int, 6> previous{};
    std::array<int, 6> positions;
    for (unsigned i = 0; i < 3; ++i) {
        positions[i] = std::clamp(int(data.accel.value[i] * 50), h - center, width - center);
        positions[i + 3] = std::clamp(int(data.gyro.value[i] / 2), h - center, width - center);
    }
    if (positions == previous) return;

    display.startWrite();
    display.setClipRect(h, h, width - h, display.height() - h);
    for (unsigned i = 0; i < positions.size(); ++i) {
        const int target = positions[i];
        int old = previous[i];
        const int y = h * (i + 2);
        if ((target < 0) != (old < 0)) {
            if (old) display.fillRect(center + std::min(old, 0), y, std::abs(old), h, TFT_BLACK);
            old = 0;
        }
        if (old == target) continue;
        const bool grows = std::abs(target) > std::abs(old);
        display.fillRect(center + std::min(old, target), y, std::abs(target - old), h,
                         grows ? colors[i] : uint16_t(TFT_BLACK));
    }
    display.clearClipRect();
    display.endWrite();
    previous = positions;
    std::printf("IMU bars: %d %d %d %d %d %d\n", positions[0], positions[1], positions[2],
                positions[3], positions[4], positions[5]);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_VERBOSE);
    M5_LOGE("Sample error log");
    M5_LOGW("Sample warning log");
    M5_LOGI("Sample info log");
    M5_LOGD("Sample debug log");
    M5_LOGV("Sample verbose log");

    auto config = M5.config();
    config.internal_rtc = false;
    config.internal_spk = false;
    config.internal_mic = false;
    config.external_rtc = false;
    config.external_imu = false;
    config.external_speaker_value = 0;
    config.external_display_value = 0;
    M5.begin(config);

    const char* board = M5.getBoard() == m5::board_t::board_M5AtomS3 ? "ATOMS3" : "unknown";
    const auto imu = M5.Imu.getType();
    const char* imuName = imu == m5::imu_mpu6886 ? "MPU6886" : imu == m5::imu_none ? "none" : "unknown";
    showBoard(board, imuName);
    M5_LOGI("core:%s", board);
    M5_LOGI("imu:%s", imuName);
    const auto& bus = M5.In_I2C;
    std::printf("IMU I2C%d SDA=%d SCL=%d\n", int(bus.getPort()), int(bus.getSDA()), int(bus.getSCL()));
    const int h = display.height() / 8;
    for (;;) {
        M5.update();
        updateButton(h);
        updateBattery();
        updateImu(h);
        vTaskDelay(1);
    }
}
