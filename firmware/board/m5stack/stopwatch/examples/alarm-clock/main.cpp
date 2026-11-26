// Adapted from M5Stack's M5StopWatch-UserDemo/app_alarm_clock (MIT).
#include "../common/round_display.hpp"
#include "../common/rtc_clock.hpp"
#include "eh_agent.h"
#include "esp_timer.h"
#include "nvs.h"
#include <cstdio>

enum class Field { hour, minute, enabled };

static void draw_alarm(int hour, int minute, bool enabled, Field field)
{
    M5.Display.clear();
    char value[24];
    snprintf(value, sizeof(value), "%02d:%02d", hour, minute);
    centered(value, 173, 6);
    centered(enabled ? "Alarm ON" : "Alarm OFF", 241, 3, enabled ? TFT_GREEN : TFT_LIGHTGREY);
    const char *fields[] = {"A: edit hour", "A: edit minute", "A: toggle alarm"};
    centered(fields[static_cast<int>(field)], 305, 2);
    centered("B: next field", 339, 2);
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    RtcClock clock;
    nvs_handle_t store;
    ESP_ERROR_CHECK(nvs_open("sw_alarm", NVS_READWRITE, &store));
    uint16_t saved = 7 * 60;
    const esp_err_t read = nvs_get_u16(store, "daily", &saved);
    if (read != ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(read);
    int minutes = (saved & 0x7fff) % 1440;
    int hour = minutes / 60, minute = minutes % 60;
    bool enabled = (saved & 0x8000) != 0;
    Field field = Field::hour;
    bool ringing = false;
    int64_t last_tone = 0;
    int64_t last_rang_minute = -1;
    draw_alarm(hour, minute, enabled, field);
    for (;;) {
        M5.update();
        if (ringing) {
            if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.Touch.getDetail().wasPressed()) {
                ringing = false;
                M5.Speaker.stop();
                draw_alarm(hour, minute, enabled, field);
            } else if (esp_timer_get_time() - last_tone >= 800000) {
                M5.Speaker.tone(1760, 100);
                last_tone = esp_timer_get_time();
            }
        } else if (M5.BtnA.wasPressed()) {
            switch (field) {
                case Field::hour: hour = (hour + 1) % 24; break;
                case Field::minute: minute = (minute + 1) % 60; break;
                case Field::enabled: enabled = !enabled; break;
            }
            ESP_ERROR_CHECK(nvs_set_u16(store, "daily", hour * 60 + minute + (enabled ? 0x8000 : 0)));
            ESP_ERROR_CHECK(nvs_commit(store));
            draw_alarm(hour, minute, enabled, field);
        } else if (M5.BtnB.wasPressed()) {
            field = static_cast<Field>((static_cast<int>(field) + 1) % 3);
            draw_alarm(hour, minute, enabled, field);
        }

        m5::rtc_datetime_t now{};
        if (clock.read(now)) {
            const int date = now.date.year * 10000 + now.date.month * 100 + now.date.date;
            const int64_t current_minute = static_cast<int64_t>(date) * 1440 + now.time.hours * 60 + now.time.minutes;
            if (!ringing && enabled && last_rang_minute != current_minute &&
                now.time.hours == hour && now.time.minutes == minute) {
                last_rang_minute = current_minute;
                ringing = true;
                last_tone = 0;
                M5.Display.clear();
                centered("Alarm", 200, 5, TFT_YELLOW);
                centered("Press a key to dismiss", 270, 2);
            }
            if (!ringing) {
                char time_label[24];
                snprintf(time_label, sizeof(time_label), "UTC %02d:%02d:%02d", now.time.hours, now.time.minutes, now.time.seconds);
                centered(time_label, 103, 2);
            }
        } else {
            centered("RTC unavailable", 103, 2);
        }
        M5.delay(50);
    }
}
