#pragma once

#include <M5Unified.h>
#include "eh_agent_ext.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include <ctime>

// Keep displaying the RX8130 offline. An SNTP sync also repairs an unset RTC
// after its backup supply has discharged. All writes happen on the UI task.
class RtcClock {
public:
    bool read(m5::rtc_datetime_t &now)
    {
        const int64_t ticks = esp_timer_get_time();
        if (!started_ && ticks >= retry_at_ && eh_net_has_ip()) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            started_ = esp_netif_sntp_init(&config) == ESP_OK;
            retry_at_ = ticks + 60000000;
        }
        if (started_ && esp_netif_sntp_sync_wait(0) == ESP_OK) {
            const std::time_t epoch = std::time(nullptr);
            std::tm utc{};
            if (gmtime_r(&epoch, &utc)) M5.Rtc.setDateTime(&utc);
        }
        return M5.Rtc.getDateTime(&now) && now.isValid();
    }

private:
    bool started_ = false;
    int64_t retry_at_ = 0;
};
