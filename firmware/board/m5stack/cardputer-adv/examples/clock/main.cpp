// Adapted for EspHub from https://github.com/m5stack/M5Cardputer-UserDemo/blob/9d7190317d49ff87fa98db345b049ed77c5a1780/main/apps/app_clock/app_clock.cpp
#include <ctime>
#include <M5Unified.h>
#include "eh_agent.h"
#include "eh_agent_ext.h"
#include "esp_netif_sntp.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.setTextSize(2);
    M5.Display.println("Waiting for time...");
    while (!eh_net_has_ip()) M5.delay(500);
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    ESP_ERROR_CHECK(esp_netif_sntp_sync_wait(portMAX_DELAY));
    M5.Display.clear();
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    for (;;) {
        std::time_t now = std::time(nullptr);
        std::tm utc{};
        if (gmtime_r(&now, &utc)) {
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%d UTC\n%H:%M:%S", &utc);
            M5.Display.setCursor(0, 0);
            M5.Display.print(text);
        }
        M5.delay(1000);
    }
}
