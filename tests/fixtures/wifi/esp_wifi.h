/* Only the ESP-IDF Wi-Fi fields used by the production manager. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;

typedef enum { WIFI_FAST_SCAN, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WIFI_CONNECT_AP_BY_SIGNAL 1
#define WIFI_IF_STA 0
#define WPA3_SAE_PWE_BOTH 3

typedef struct {
    bool show_hidden;
    int scan_type;
    struct { struct { uint32_t min, max; } active; } scan_time;
} wifi_scan_config_t;

typedef struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t primary;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        wifi_scan_method_t scan_method;
        bool bssid_set;
        uint8_t bssid[6];
        uint8_t channel;
        int sort_method;
        struct { wifi_auth_mode_t authmode; } threshold;
        struct { bool capable; } pmf_cfg;
        int sae_pwe_h2e;
    } sta;
} wifi_config_t;

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
