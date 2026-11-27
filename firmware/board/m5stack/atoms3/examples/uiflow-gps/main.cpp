// SPDX-FileCopyrightText: 2024-2025 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr auto gpsUart = UART_NUM_2;
constexpr int rxPin = 5;

bool digits(const char* text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

int pair(const char* text)
{
    return (text[0] - '0') * 10 + text[1] - '0';
}

bool decimal(const char* text, double& result)
{
    const char* number = text + (*text == '-' || *text == '+');
    const size_t integer = std::strspn(number, "0123456789");
    if (integer == 0) {
        return false;
    }
    const char* tail = number + integer;
    if (*tail == '.') {
        ++tail;
        tail += std::strspn(tail, "0123456789");
    }
    if (*tail) {
        return false;
    }
    result = std::strtod(text, nullptr);
    return std::isfinite(result);
}

bool coordinate(const char* text, const char* direction, size_t degreeDigits,
                char positive, char negative, double limit, double& result)
{
    if (std::strlen(text) < degreeDigits + 2 || !digits(text, degreeDigits + 2)
        || (text[degreeDigits + 2] && text[degreeDigits + 2] != '.')
        || (*direction != positive && *direction != negative) || direction[1] != '\0') {
        return false;
    }
    double minutes;
    if (!decimal(text + degreeDigits, minutes) || minutes >= 60) {
        return false;
    }
    int degrees = 0;
    for (size_t i = 0; i < degreeDigits; ++i) {
        degrees = degrees * 10 + text[i] - '0';
    }
    if (degrees > limit || (degrees == limit && minutes > 0)) {
        return false;
    }
    const double value = degrees + std::round(minutes / 60 * 1000000) / 1000000;
    result = *direction == negative ? -value : value;
    return true;
}

bool leapYear(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int daysInMonth(int year, int month)
{
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && leapYear(year));
}

struct DateTime {
    int year, month, day, hour, minute, second;

    bool read(const char* date, const char* time)
    {
        if (std::strlen(date) != 6 || !digits(date, 6) || std::strlen(time) < 6
            || !digits(time, 6) || (time[6] && (time[6] != '.' || !time[7]
                                              || !digits(time + 7, std::strlen(time + 7))))) {
            return false;
        }
        day = pair(date);
        month = pair(date + 2);
        year = 2000 + pair(date + 4);
        hour = pair(time);
        minute = pair(time + 2);
        second = pair(time + 4);
        return month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month)
            && hour < 24 && minute < 60 && second <= 60;
    }

    int64_t timestamp() const
    {
        // UiFlow2's ESP32 modtime patch selects the Unix epoch.
        int days = day - 1;
        for (int y = 1970; y < year; ++y) {
            days += 365 + leapYear(y);
        }
        for (int m = 1; m < month; ++m) {
            days += daysInMonth(year, m);
        }
        return int64_t(days) * 86400 + hour * 3600 + minute * 60 + second;
    }
};

int hexDigit(char byte)
{
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 : -1;
}

class GpsReport {
public:
    void receive(char byte)
    {
        if (byte == '$') {
            size = 0;
            collecting = true;
        } else if (collecting && (byte == '\r' || byte == '\n')) {
            sentence[size] = '\0';
            collecting = false;
            parse();
        } else if (collecting) {
            if (size + 1 < sizeof(sentence) && byte >= ' ' && byte <= '~') {
                sentence[size++] = byte;
            } else {
                collecting = false;
            }
        }
    }

    void print() const
    {
        if (dateTime.year) {
            const auto& t = dateTime;
            std::printf("[%d, %d, %d]\n[%d, %d, %d]\n[%d, %d, %d, %d, %d, %d]\n"
                        "%lld\n%.6f\n%.6f\n",
                        t.hour, t.minute, t.second, t.year, t.month, t.day,
                        t.year, t.month, t.day, t.hour, t.minute, t.second,
                        static_cast<long long>(t.timestamp()), latitude, longitude);
        } else {
            std::puts("['00', '00', '00']\n['00', '00', '00']\n"
                      "['00', '00', '00', '00', '00', '00']\n0\n0N\n0E");
        }
        std::printf("%s\n%s\n%s\n%s\n%s\n", altitude, satellites, quality, course, speed);
    }

private:
    void parse()
    {
        char* checksum = std::strchr(sentence, '*');
        if (!checksum || std::strlen(checksum) != 3) {
            return;
        }
        const int high = hexDigit(checksum[1]);
        const int low = hexDigit(checksum[2]);
        unsigned char actual = 0;
        for (const char* p = sentence; p != checksum; ++p) {
            actual ^= *p;
        }
        if (high < 0 || low < 0 || actual != 16 * high + low) {
            return;
        }
        *checksum = '\0';
        const char* fields[20] = {sentence};
        size_t count = 1;
        for (char* p = sentence; *p; ++p) {
            if (*p == ',') {
                if (count == sizeof(fields) / sizeof(fields[0])) {
                    return;
                }
                *p = '\0';
                fields[count++] = p + 1;
            }
        }
        if (std::strlen(fields[0]) != 5) {
            return;
        }
        // TXT only changes the upstream antenna getter, which this example does not print.
        if (count >= 11 && std::strcmp(fields[0] + 2, "GGA") == 0) {
            updateGga(fields);
        } else if (count >= 10 && std::strcmp(fields[0] + 2, "RMC") == 0) {
            updateRmc(fields);
        }
    }

    void updateGga(const char* const* fields)
    {
        const char* fix = fields[6];
        if (std::strlen(fix) != 1 || !digits(fix, 1)) {
            return;
        }
        if (*fix == '0') {
            quality[0] = '0';
            return;
        }
        const size_t satLength = std::strlen(fields[7]);
        const size_t altLength = std::strlen(fields[9]);
        double altitudeValue;
        if (satLength == 0 || satLength >= sizeof(satellites) || !digits(fields[7], satLength)
            || (altLength && (altLength + 1 >= sizeof(altitude) || std::strcmp(fields[10], "M") != 0
                              || !decimal(fields[9], altitudeValue)))) {
            return;
        }
        quality[0] = *fix;
        std::strcpy(satellites, fields[7]);
        if (altLength) {
            std::snprintf(altitude, sizeof(altitude), "%sM", fields[9]);
        }
    }

    void updateRmc(const char* const* fields)
    {
        if (std::strcmp(fields[2], "A") != 0) {
            return;
        }
        DateTime next;
        double lat, lon, fieldValue;
        if (!next.read(fields[9], fields[1])
            || !coordinate(fields[3], fields[4], 2, 'N', 'S', 90, lat)
            || !coordinate(fields[5], fields[6], 3, 'E', 'W', 180, lon)
            || std::strlen(fields[7]) >= sizeof(speed) || std::strlen(fields[8]) >= sizeof(course)
            || (*fields[7] && (!decimal(fields[7], fieldValue) || fieldValue < 0))
            || (*fields[8] && (!decimal(fields[8], fieldValue) || fieldValue < 0 || fieldValue > 360))) {
            return;
        }
        dateTime = next;
        latitude = lat;
        longitude = lon;
        std::strcpy(speed, fields[7]);
        std::strcpy(course, fields[8]);
    }

    char sentence[128]{};
    size_t size = 0;
    bool collecting = false;
    DateTime dateTime{};
    double latitude = 0, longitude = 0;
    char altitude[20] = "0";
    char satellites[4] = "0";
    char quality[2] = "0";
    char course[16] = "0";
    char speed[16] = "0";
};
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setFont(&fonts::DejaVu18);
    display.setTextDatum(top_left);
    display.fillRect(0, 0, display.width(), display.fontHeight(), TFT_BLUE);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.drawString("GPS Base", 3, 0);

    const uart_config_t config{
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    ESP_ERROR_CHECK(uart_param_config(gpsUart, &config));
    // The GPS only sends data. GPIO6 is wired to the base's MicroSD MOSI.
    ESP_ERROR_CHECK(uart_set_pin(gpsUart, UART_PIN_NO_CHANGE, rxPin,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(gpsUart, 1024, 0, 0, nullptr, 0));
    std::puts("GPS Base: UART2 RX=GPIO5 9600 8N1, UTC");

    GpsReport report;
    uint8_t data[128];
    int64_t nextReport = 0;
    for (;;) {
        M5.update();
        const int count = uart_read_bytes(gpsUart, data, sizeof(data), pdMS_TO_TICKS(25));
        for (int i = 0; i < count; ++i) {
            report.receive(data[i]);
        }
        const int64_t now = esp_timer_get_time();
        if (now >= nextReport) {
            report.print();
            nextReport = now + 1000000;
        }
    }
}
