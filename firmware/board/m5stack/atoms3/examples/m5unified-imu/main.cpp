// SPDX-FileCopyrightText: 2021 M5Stack
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace {
struct rect_t
{
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
};

static constexpr const uint32_t color_tbl[18] =
{
0xFF0000u, 0xCCCC00u, 0xCC00FFu,
0xFFCC00u, 0x00FF00u, 0x0088FFu,
0xFF00CCu, 0x00FFCCu, 0x0000FFu,
0xFF0000u, 0xCCCC00u, 0xCC00FFu,
0xFFCC00u, 0x00FF00u, 0x0088FFu,
0xFF00CCu, 0x00FFCCu, 0x0000FFu,
};
static constexpr const float coefficient_tbl[3] = { 0.5f, (1.0f / 256.0f), (1.0f / 1024.0f) };

static auto &dsp = (M5.Display);
static rect_t rect_graph_area;
static rect_t rect_text_area;

static uint8_t calib_countdown = 0;
static constexpr uint8_t calib_value = 64;
static uint32_t lastCalibrationTick;

static int prev_xpos[18];

void drawBar(int32_t ox, int32_t oy, int32_t nx, int32_t px, int32_t h, uint32_t color)
{
  uint32_t bgcolor = (color >> 3) & 0x1F1F1Fu;
  if (px && ((nx < 0) != (px < 0)))
  {
    dsp.fillRect(ox, oy, px, h, bgcolor);
    px = 0;
  }
  if (px != nx)
  {
    if ((nx > px) != (nx < 0))
    {
      bgcolor = color;
    }
    dsp.setColor(bgcolor);
    dsp.fillRect(nx + ox, oy, px - nx, h);
  }
}

void drawGraph(const rect_t& r, const m5::imu_data_t& data)
{
  int ox = (r.x + r.w)>>1;
  int oy = r.y;
  int h = (r.h / 18) * (calib_countdown ? 1 : 2);
  int bar_count = 9 * (calib_countdown ? 2 : 1);

  dsp.startWrite();
  for (int index = 0; index < bar_count; ++index)
  {
    float xval;
    if (index < 9)
    {
      auto coe = coefficient_tbl[index / 3] * r.w;
      xval = data.value[index] * coe;
    }
    else
    {
      xval = M5.Imu.getOffsetData(index - 9) * (1.0f / (1 << 19));
    }

    const int nx = static_cast<int>(xval);
    int px = prev_xpos[index];
    prev_xpos[index] = nx;
    drawBar(ox, oy + h * index, nx, px, h - 1, color_tbl[index]);
  }
  dsp.endWrite();
}

void reportOffsets(const char* state)
{
  std::printf("IMU offsets %s:", state);
  for (unsigned i = 0; i < 9; ++i) {
    std::printf(" %ld", long(M5.Imu.getOffsetData(i)));
  }
  std::puts("");
}

void updateCalibration(uint8_t remaining, bool start = false)
{
  calib_countdown = remaining;
  if (start || remaining == 0) {
    std::memset(prev_xpos, 0, sizeof(prev_xpos));
    dsp.fillScreen(TFT_BLACK);
    if (start) {
      M5.Imu.setCalibration(calib_value, calib_value, calib_value);
    } else {
      M5.Imu.setCalibration(0, 0, calib_value);
      reportOffsets(M5.Imu.saveOffsetToNVS() ? "saved" : "save failed");
      std::puts("IMU normal: 9 bars");
    }
  }

  auto& area = rect_text_area;
  dsp.fillRect(area.x, area.y, area.w, area.h, remaining ? TFT_BLUE : TFT_BLACK);
  if (remaining) {
    dsp.setCursor(area.x + 2, area.y + 1);
    dsp.setTextColor(TFT_WHITE, TFT_BLUE);
    dsp.printf("Countdown:%u", unsigned(remaining));
    std::printf("Countdown:%u (18 bars)\n", unsigned(remaining));
    reportOffsets("calibration");
  }
}

void startCalibration(const char* reason)
{
  lastCalibrationTick = M5.millis();
  std::printf("IMU calibration: %s\n", reason);
  updateCalibration(10, true);
}
}

extern "C" void app_main()
{
  ESP_ERROR_CHECK(eh_agent_start());
  M5.begin(M5.config());

  const auto type = M5.Imu.getType();
  const char* name = type == m5::imu_mpu6886 ? "mpu6886" :
                     type == m5::imu_none ? "not found" : "unknown";
  M5_LOGI("imu:%s", name);
  dsp.printf("imu:%s", name);
  if (type == m5::imu_none) return;

  const auto& bus = M5.In_I2C;
  std::printf("IMU I2C%d SDA=%d SCL=%d\n", int(bus.getPort()), int(bus.getSDA()), int(bus.getSCL()));
  const int32_t width = dsp.width();
  const int32_t height = dsp.height();
  const int32_t graphHeight = ((height - 8) / 18) * 18;
  rect_graph_area = {0, 0, width, graphHeight};
  rect_text_area = {0, graphHeight, width, height - graphHeight};
  // Fit "Countdown:10" on a single 128-pixel line at the upstream height.
  dsp.setTextSize(1.5f, rect_text_area.h / 8);

  if (M5.Imu.loadOffsetFromNVS()) {
    dsp.fillRect(0, 0, width, graphHeight, TFT_BLACK);
    dsp.setCursor(2, graphHeight + 1);
    dsp.setTextColor(TFT_WHITE, TFT_BLACK);
    dsp.printf("imu:%s", name);
    reportOffsets("loaded");
    std::puts("IMU normal: 9 bars");
  } else {
    startCalibration("empty NVS");
  }

  uint32_t lastReport = M5.millis();
  unsigned frameCount = 0;
  m5::imu_data_t data{};
  for (;;) {
    M5.update();
    if (M5.BtnA.wasClicked()) startCalibration("BtnA");
    if (M5.Imu.update()) {
      data = M5.Imu.getImuData();
      drawGraph(rect_graph_area, data);
      ++frameCount;
    }

    const uint32_t now = M5.millis();
    if (now - lastReport >= 1000) {
      lastReport += 1000;
      M5_LOGI("sec:%u  frame:%u", unsigned(now / 1000), frameCount);
      if (frameCount) {
        std::printf("IMU sample acc=%.4f,%.4f,%.4f gyro=%.4f,%.4f,%.4f mag=%.4f,%.4f,%.4f\n",
                    data.accel.x, data.accel.y, data.accel.z, data.gyro.x, data.gyro.y, data.gyro.z,
                    data.mag.x, data.mag.y, data.mag.z);
      }
      frameCount = 0;
    }
    if (calib_countdown && now - lastCalibrationTick >= 1000) {
      lastCalibrationTick += 1000;
      updateCalibration(calib_countdown - 1);
    }
    vTaskDelay(1);
  }
}
