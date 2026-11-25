/* Network bring-up: Wi-Fi STA (default) or OpenCores Ethernet for QEMU.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_check.h"
#include "sdkconfig.h"
#if CONFIG_OKOU_NET_OPENETH
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#endif
#include "okou_priv.h"

static volatile bool s_has_ip;
static bool s_managed;
static esp_netif_t *s_netif;
static char s_ip[16] = "";

bool okou_wifi_rssi(int *rssi)
{
#if !CONFIG_OKOU_NET_OPENETH
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        *rssi = ap.rssi;
        return true;
    }
#endif
    return false;
}

bool okou_net_has_ip(void)
{
    return s_has_ip;
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *ev = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
    if (!s_netif) {
        s_netif = ev->esp_netif;
    }
    s_has_ip = true;
    okou_agent_notify();
    ESP_LOGI(OKOU_TAG, "got ip %s", s_ip);
#if !CONFIG_OKOU_NET_OPENETH
    if (s_managed) {
        okou_wifi_on_got_ip();
    }
#endif
}

static void on_lost_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    s_has_ip = false;
    s_ip[0] = '\0';
    okou_agent_notify();
}

#if !CONFIG_OKOU_NET_OPENETH
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        okou_wifi_on_started();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_has_ip = false;
        s_ip[0] = '\0';
        okou_wifi_on_disconnected(((wifi_event_sta_disconnected_t *)data)->reason);
        okou_agent_notify();
    }
}
#endif

void okou_net_reconfigure(void)
{
#if !CONFIG_OKOU_NET_OPENETH
    if (s_managed) {
        okou_wifi_reload();
    }
#endif
}

esp_err_t okou_net_start(bool manage)
{
    s_managed = manage;
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_lost_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_lost_ip, NULL);

    if (!manage) {
        /* App owns networking; pick up an address that already exists. */
        const char *keys[] = {"WIFI_STA_DEF", "ETH_DEF"};
        for (int i = 0; i < 2; i++) {
            esp_netif_t *n = esp_netif_get_handle_from_ifkey(keys[i]);
            esp_netif_ip_info_t ip;
            if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr) {
                s_netif = n;
                snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip.ip));
                s_has_ip = true;
            }
        }
        return ESP_OK;
    }

    char name[33] = {0};
    okou_nvs_get_str("name", name, sizeof(name));

#if CONFIG_OKOU_NET_OPENETH
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&config, &eth), OKOU_TAG, "eth install");
    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&ncfg);
    if (name[0]) {
        esp_netif_set_hostname(s_netif, name);
    }
    esp_netif_attach(s_netif, esp_eth_new_netif_glue(eth));
    ESP_LOGI(OKOU_TAG, "network: OpenCores Ethernet (QEMU)");
    return esp_eth_start(eth);
#else
    s_netif = esp_netif_create_default_wifi_sta();
    if (name[0]) {
        esp_netif_set_hostname(s_netif, name);
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), OKOU_TAG, "wifi init");
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    /* Load saved networks before the driver switches to RAM storage */
    ESP_RETURN_ON_ERROR(okou_wifi_init(), OKOU_TAG, "wifi manager");
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* networks live in okl/wifis, not in the driver */
    esp_wifi_set_mode(WIFI_MODE_STA);
    return esp_wifi_start();
#endif
}

void okou_net_info(cJSON *obj)
{
    cJSON_AddStringToObject(obj, "ip", s_ip);
#if !CONFIG_OKOU_NET_OPENETH
    wifi_ap_record_t ap;
    if (s_has_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(obj, "rssi", ap.rssi);
        return;
    }
#endif
    cJSON_AddNullToObject(obj, "rssi");
}

int okou_net_describe(char *buf, size_t len)
{
#if CONFIG_OKOU_NET_OPENETH
    return snprintf(buf, len, "ethernet(openeth) ip=%s %s", s_ip[0] ? s_ip : "-", s_has_ip ? "up" : "down");
#else
    wifi_ap_record_t ap;
    if (s_has_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return snprintf(buf, len, "wifi ssid=%s rssi=%d ch=%d ip=%s", (char *)ap.ssid, ap.rssi, ap.primary, s_ip);
    }
    return snprintf(buf, len, "wifi %s ip=%s saved=%d%s", s_has_ip ? "up" : "down", s_ip[0] ? s_ip : "-",
                    okou_wifi_count(), okou_wifi_count() || !s_managed ? "" : " (no credentials)");
#endif
}
