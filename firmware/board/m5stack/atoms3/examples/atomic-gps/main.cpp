// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicGPS / GPS example.
#include "eh_agent.h"
#include "minmea.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr auto gpsUart = UART_NUM_2;
constexpr int rxPin = 5;
constexpr int baudRate = 9600;
constexpr int64_t secondUs = 1000000;
constexpr double radians = 3.14159265358979323846 / 180.0;
constexpr double londonLatitude = 51.508131;
constexpr double londonLongitude = -0.128002;

double coordinate(const minmea_float& field)
{
    if (field.scale <= 0) {
        return NAN;
    }
    const double packed = double(field.value) / field.scale;
    const double degrees = std::trunc(packed / 100);
    const double minutes = packed - degrees * 100;
    return std::abs(minutes) < 60 ? degrees + minutes / 60 : NAN;
}

const char* cardinal(double course)
{
    const char* const directions[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    };
    if (!std::isfinite(course) || course < 0 || course > 360) {
        return "-";
    }
    return directions[unsigned(std::round(course / 22.5)) % 16];
}

class GpsStatus {
public:
    void receive(char byte)
    {
        ++characters;
        if (byte == '$') {
            size = 0;
            collecting = true;
        }
        if (!collecting) {
            return;
        }
        if (byte == '\r' || byte == '\n') {
            sentence[size] = '\0';
            collecting = false;
            parse();
        } else if (size + 1 < sizeof(sentence)) {
            sentence[size++] = byte;
        } else {
            collecting = false;
        }
    }

    void report(int64_t now) const
    {
        std::printf("GPS %s sats=%d hdop=%.1f ", fixed ? "FIX" : "NO_FIX", satellites, hdop);
        if (fixed) {
            const auto ageMs = static_cast<long long>((now - lastFixUs) / 1000);
            std::printf("lat=%.6f lon=%.6f age_ms=%lld ", latitude, longitude, ageMs);
        }
        if (date.year >= 0) {
            std::printf("date=%02d/%02d/%02d ", date.month, date.day, date.year);
        }
        if (time.hours >= 0) {
            std::printf("utc=%02d:%02d:%02d ", time.hours, time.minutes, time.seconds);
        }
        if (fixed) {
            std::printf("alt_m=%.2f course_deg=%.2f speed_kmh=%.2f heading=%s ",
                        altitude, course, speed, cardinal(course));
            reportLondon();
        }
        std::printf("chars=%lu fixes=%lu checksum_errors=%lu\n", characters, fixes, checksumErrors);
        if (characters < 10 && now > 5 * secondUs) {
            std::puts("No GPS data received: check wiring");
        }
    }

private:
    void updateFix(bool valid, const minmea_float& lat, const minmea_float& lon)
    {
        const double parsedLat = coordinate(lat);
        const double parsedLon = coordinate(lon);
        fixed = valid && std::abs(parsedLat) <= 90 && std::abs(parsedLon) <= 180;
        if (fixed) {
            latitude = parsedLat;
            longitude = parsedLon;
            lastFixUs = esp_timer_get_time();
            ++fixes;
        }
    }

    void parse()
    {
        if (!minmea_check(sentence, true)) {
            ++checksumErrors;
            return;
        }
        switch (minmea_sentence_id(sentence, true)) {
            case MINMEA_SENTENCE_RMC: {
                minmea_sentence_rmc frame{};
                if (minmea_parse_rmc(&frame, sentence)) {
                    updateFix(frame.valid, frame.latitude, frame.longitude);
                    date = frame.date;
                    time = frame.time;
                    speed = minmea_tofloat(&frame.speed) * 1.852;
                    course = minmea_tofloat(&frame.course);
                }
                break;
            }
            case MINMEA_SENTENCE_GGA: {
                minmea_sentence_gga frame{};
                if (minmea_parse_gga(&frame, sentence)) {
                    updateFix(frame.fix_quality > 0, frame.latitude, frame.longitude);
                    time = frame.time;
                    satellites = frame.satellites_tracked;
                    hdop = minmea_tofloat(&frame.hdop);
                    altitude = minmea_tofloat(&frame.altitude);
                }
                break;
            }
            default:
                break;
        }
    }

    void reportLondon() const
    {
        const double fromLat = latitude * radians;
        const double toLat = londonLatitude * radians;
        const double deltaLat = toLat - fromLat;
        const double deltaLon = (londonLongitude - longitude) * radians;
        const double latTerm = std::sin(deltaLat / 2);
        const double lonTerm = std::sin(deltaLon / 2);
        const double a = latTerm * latTerm + std::cos(fromLat) * std::cos(toLat) * lonTerm * lonTerm;
        const double km = 2 * 6371.009 * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
        const double y = std::sin(deltaLon) * std::cos(toLat);
        const double x = std::cos(fromLat) * std::sin(toLat)
                         - std::sin(fromLat) * std::cos(toLat) * std::cos(deltaLon);
        const double bearing = std::fmod(std::atan2(y, x) / radians + 360, 360);
        std::printf("london_km=%lu london_course_deg=%.2f london_heading=%s ",
                    static_cast<unsigned long>(km), bearing, cardinal(bearing));
    }

    char sentence[128]{};
    size_t size = 0;
    bool collecting = false;
    bool fixed = false;
    double latitude = NAN, longitude = NAN;
    double hdop = NAN, altitude = NAN, course = NAN, speed = NAN;
    int satellites = -1;
    minmea_date date{-1, -1, -1};
    minmea_time time{-1, -1, -1, -1};
    int64_t lastFixUs = 0;
    unsigned long characters = 0, fixes = 0, checksumErrors = 0;
};
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSerifBold24pt7b);
    display.drawString("GPS", display.width() / 2, display.height() / 2);

    const uart_config_t config{
        .baud_rate = baudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    ESP_ERROR_CHECK(uart_param_config(gpsUart, &config));
    ESP_ERROR_CHECK(uart_set_pin(gpsUart, UART_PIN_NO_CHANGE, rxPin,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(gpsUart, 1024, 0, 0, nullptr, 0));
    std::puts("Atomic GPS: UART2 RX=GPIO5 9600 8N1");

    GpsStatus gps;
    uint8_t data[128];
    int64_t nextReport = 0;
    for (;;) {
        const int count = uart_read_bytes(gpsUart, data, sizeof(data), pdMS_TO_TICKS(50));
        for (int i = 0; i < count; ++i) {
            gps.receive(data[i]);
        }
        const int64_t now = esp_timer_get_time();
        if (now >= nextReport) {
            gps.report(now);
            nextReport = now + secondUs;
        }
    }
}
