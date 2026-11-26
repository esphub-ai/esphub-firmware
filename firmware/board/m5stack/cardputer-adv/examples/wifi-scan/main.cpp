// Adapted for EspHub from https://github.com/m5stack/M5Cardputer-UserDemo/blob/9d7190317d49ff87fa98db345b049ed77c5a1780/main/apps/app_wifi_scan/app_wifi_scan.cpp
#include <M5Unified.h>
#include "eh_agent.h"
#include "eh_agent_ext.h"

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin();
    M5.Display.println("Scanning Wi-Fi...");
    for (;;) {
        // Share the SDK's radio lock with connection and provisioning attempts.
        eh_ap_t networks[6];
        int count = eh_wifi_scan(networks, 6, 5000);
        M5.Display.clear();
        M5.Display.setCursor(0, 0);
        M5.Display.println("Wi-Fi scan       RSSI (dBm)");
        if (count < 0) M5.Display.println("Radio busy. Retrying...");
        if (count == 0) M5.Display.println("No networks found");
        for (int i = 0; i < count; ++i) {
            M5.Display.printf("%4d  %.32s\n", networks[i].rssi, networks[i].ssid);
        }
        M5.delay(5000);
    }
}
