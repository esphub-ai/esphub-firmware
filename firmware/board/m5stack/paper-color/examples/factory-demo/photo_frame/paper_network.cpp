// SPDX-License-Identifier: MIT
#include "paper_network.h"
#include "eh_agent.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

// The captive DNS task reads this value without retaining a netif across handoff.
static std::atomic<uint32_t> hotspot_ip;
extern "C" uint32_t paper_hotspot_ipv4(void) { return hotspot_ip.load(); }

namespace paper_network {
namespace {
std::mutex radio;
esp_netif_t *access_point;

esp_err_t close_hotspot()
{
    if (!access_point) return ESP_OK;
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) return err;
    hotspot_ip = 0;
    err = esp_wifi_deinit();
    if (err != ESP_OK) return err;
    esp_netif_destroy_default_wifi(access_point);
    access_point = nullptr;
    return ESP_OK;
}
}

esp_err_t open_hotspot(const char *ssid)
{
    std::lock_guard<std::mutex> guard(radio);
#if CONFIG_EH_NET_OPENETH
    return ESP_ERR_NOT_SUPPORTED;
#else
    size_t length = strlen(ssid);
    if (!length || length > 32) return ESP_ERR_INVALID_ARG;
    if (hotspot_ip) return ESP_OK;
    esp_err_t err = close_hotspot();
    if (err != ESP_OK) return err;
    err = eh_wifi_set_managed(false);
    if (err != ESP_OK) return err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    access_point = esp_netif_create_default_wifi_ap();
    // IDF retains this pointer. Install the captive portal URI before AP_START
    // starts DHCP and advertises it to phones.
    static char uri[sizeof("http://255.255.255.255")];
    esp_netif_ip_info_t ip{};
    err = esp_netif_get_ip_info(access_point, &ip);
    if (err == ESP_OK && !ip.ip.addr) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        snprintf(uri, sizeof(uri), "http://" IPSTR, IP2STR(&ip.ip));
        esp_netif_dhcps_option(access_point, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    }
    wifi_config_t config{};
    memcpy(config.ap.ssid, ssid, length);
    config.ap.ssid_len = length;
    config.ap.channel = 6;
    config.ap.authmode = WIFI_AUTH_OPEN;
    config.ap.max_connection = 4;
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_err_t cleanup = close_hotspot();
        return cleanup == ESP_OK ? err : cleanup;
    }
    hotspot_ip = ip.ip.addr;
    ESP_LOGI("PaperColor", "Setup hotspot %s started", ssid);
    return ESP_OK;
#endif
}

esp_err_t use_station()
{
    std::lock_guard<std::mutex> guard(radio);
    esp_err_t err = close_hotspot();
    return err == ESP_OK ? eh_wifi_set_managed(true) : err;
}

uint32_t station_ipv4()
{
    std::lock_guard<std::mutex> guard(radio);
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return station && esp_netif_get_ip_info(station, &ip) == ESP_OK ? ip.ip.addr : 0;
}

int hotspot_clients()
{
    std::lock_guard<std::mutex> guard(radio);
    if (!hotspot_ip) return -1;
    wifi_sta_list_t clients;
    return esp_wifi_ap_get_sta_list(&clients) == ESP_OK ? clients.num : -1;
}

int scan(eh_ap_t *results, int capacity, uint32_t timeout_ms)
{
    std::lock_guard<std::mutex> guard(radio);
    if (!hotspot_ip) return eh_wifi_scan(results, capacity, timeout_ms);

    // IDF can scan in APSTA mode without creating a station netif or joining
    // a network. Keep the phone associated throughout the setup-page scan.
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return -1;
    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    wifi_ap_record_t records[20];
    uint16_t count = sizeof(records) / sizeof(records[0]);
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    esp_err_t restored = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK || restored != ESP_OK) return -1;
    int found = 0;
    for (int i = 0; i < count && found < capacity; ++i) {
        const char *ssid = reinterpret_cast<const char *>(records[i].ssid);
        if (!*ssid) continue;
        int j = 0;
        while (j < found && strcmp(results[j].ssid, ssid)) ++j;
        if (j < found) continue; // IDF returns the strongest observation first
        auto &ap = results[found++];
        strlcpy(ap.ssid, ssid, sizeof(ap.ssid));
        ap.rssi = records[i].rssi;
        ap.auth = records[i].authmode;
        ap.channel = records[i].primary;
        memcpy(ap.bssid, records[i].bssid, sizeof(ap.bssid));
    }
    return found;
}
}
