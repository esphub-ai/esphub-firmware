/* Network bring-up: Wi-Fi STA (default) or OpenCores Ethernet for QEMU.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdatomic.h>
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
#if CONFIG_EH_NET_OPENETH
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#endif
#include "eh_priv.h"

/* A station handoff must never clear an independent Ethernet uplink. */
static _Atomic uint32_t s_sta_ip;
static _Atomic uint32_t s_eth_ip;
#if CONFIG_LWIP_PPP_SUPPORT
static _Atomic uint32_t s_ppp_ip;
#endif

static uint32_t uplink_ip(void)
{
    uint32_t ip = s_sta_ip;
    if (!ip) ip = s_eth_ip;
#if CONFIG_LWIP_PPP_SUPPORT
    if (!ip) ip = s_ppp_ip;
#endif
    return ip;
}

bool eh_net_has_ip(void)
{
    return uplink_ip() != 0;
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    _Atomic uint32_t *address = arg;
    bool got = id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP;
#if CONFIG_LWIP_PPP_SUPPORT
    got = got || id == IP_EVENT_PPP_GOT_IP;
#endif
    if (got) {
        ip_event_got_ip_t *ev = data;
        *address = ev->ip_info.ip.addr;
        ESP_LOGI(EH_TAG, "got ip " IPSTR, IP2STR(&ev->ip_info.ip));
#if !CONFIG_EH_NET_OPENETH
        if (id == IP_EVENT_STA_GOT_IP) eh_wifi_on_got_ip();
#endif
    } else {
        *address = 0;
    }
    eh_agent_notify();
}

#if !CONFIG_EH_NET_OPENETH
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        eh_wifi_on_started();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_ip = 0;
        eh_wifi_on_disconnected(((wifi_event_sta_disconnected_t *)data)->reason);
        eh_agent_notify();
    } else if (id == WIFI_EVENT_STA_STOP) {
        s_sta_ip = 0;
        eh_agent_notify();
    }
}
#endif

void eh_net_reconfigure(void)
{
#if !CONFIG_EH_NET_OPENETH
    eh_wifi_reload();
#endif
}

static esp_err_t observe_uplink(const char *key, int got, int lost, _Atomic uint32_t *address)
{
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, got, on_ip, address), EH_TAG, "watch IP");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, lost, on_ip, address), EH_TAG, "watch IP loss");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_is_netif_up(netif) && esp_netif_get_ip_info(netif, &ip) == ESP_OK)
        *address = ip.ip.addr;
    return ESP_OK;
}

esp_err_t eh_net_start(bool manage)
{
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    ESP_RETURN_ON_ERROR(observe_uplink("WIFI_STA_DEF", IP_EVENT_STA_GOT_IP, IP_EVENT_STA_LOST_IP, &s_sta_ip),
                        EH_TAG, "station IP");
    ESP_RETURN_ON_ERROR(observe_uplink("ETH_DEF", IP_EVENT_ETH_GOT_IP, IP_EVENT_ETH_LOST_IP, &s_eth_ip),
                        EH_TAG, "Ethernet IP");
#if CONFIG_LWIP_PPP_SUPPORT
    ESP_RETURN_ON_ERROR(observe_uplink("PPP_DEF", IP_EVENT_PPP_GOT_IP, IP_EVENT_PPP_LOST_IP, &s_ppp_ip),
                        EH_TAG, "PPP IP");
#endif

#if CONFIG_EH_NET_OPENETH
    if (!manage) return ESP_OK;
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&config, &eth), EH_TAG, "eth install");
    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&ncfg);
    esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
    ESP_LOGI(EH_TAG, "network: OpenCores Ethernet (QEMU)");
    return esp_eth_start(eth);
#else
    ESP_RETURN_ON_ERROR(eh_wifi_init(), EH_TAG, "wifi manager");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL), EH_TAG, "watch station");
    return eh_wifi_set_managed(manage);
#endif
}

static void format_ip(char buf[16])
{
    esp_ip4_addr_t ip = {.addr = uplink_ip()};
    buf[0] = '\0';
    if (ip.addr) snprintf(buf, 16, IPSTR, IP2STR(&ip));
}

void eh_net_info(cJSON *obj)
{
    char ip[16];
    format_ip(ip);
    cJSON_AddStringToObject(obj, "ip", ip);
#if !CONFIG_EH_NET_OPENETH
    wifi_ap_record_t ap;
    if (s_sta_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(obj, "rssi", ap.rssi);
        return;
    }
#endif
    cJSON_AddNullToObject(obj, "rssi");
}

int eh_net_describe(char *buf, size_t len)
{
    char ip[16];
    format_ip(ip);
#if CONFIG_EH_NET_OPENETH
    return snprintf(buf, len, "ethernet(openeth) ip=%s %s", ip[0] ? ip : "-", ip[0] ? "up" : "down");
#else
    wifi_ap_record_t ap;
    if (s_sta_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return snprintf(buf, len, "wifi ssid=%s rssi=%d ch=%d ip=%s", (char *)ap.ssid, ap.rssi, ap.primary, ip);
    }
    if (ip[0]) return snprintf(buf, len, "network up ip=%s", ip);
    return snprintf(buf, len, "wifi down ip=- saved=%d", eh_wifi_count());
#endif
}
