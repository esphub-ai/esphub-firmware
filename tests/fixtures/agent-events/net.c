/* Network callbacks must wake a portMAX_DELAY agent on IP loss, not just gain. */
#include "fake-idf.h"
#include <stdio.h>
#include <stdlib.h>

typedef const char *esp_event_base_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { esp_netif_t *esp_netif; esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
typedef struct { int rssi, primary; unsigned char ssid[33]; } wifi_ap_record_t;
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { int unused; } wifi_init_config_t;
#define IP_EVENT "IP"
#define WIFI_EVENT "WIFI"
#define IP_EVENT_STA_GOT_IP 1
#define IP_EVENT_ETH_GOT_IP 2
#define IP_EVENT_STA_LOST_IP 3
#define IP_EVENT_ETH_LOST_IP 4
#define WIFI_EVENT_STA_START 5
#define WIFI_EVENT_STA_DISCONNECTED 6
#define ESP_EVENT_ANY_ID -1
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 10u, 0u, 0u, (unsigned)((ip)->addr)
#define ESP_RETURN_ON_ERROR(expr, ...) do { esp_err_t e_ = (expr); if (e_ != ESP_OK) return e_; } while (0)
static esp_err_t esp_netif_init(void) { return ESP_OK; }
static esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { return NULL; }
static esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip) { return ESP_FAIL; }
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return NULL; }
static esp_err_t esp_netif_set_hostname(esp_netif_t *n, const char *name) { return ESP_OK; }
static esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) { return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int storage) { return ESP_OK; }
static esp_err_t esp_wifi_set_mode(int mode) { return ESP_OK; }
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { return ESP_FAIL; }
typedef void (*event_handler_t)(void *, esp_event_base_t, int32_t, void *);
static event_handler_t handlers[7];
static esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, event_handler_t handler, void *arg)
{ if (id == ESP_EVENT_ANY_ID) id = WIFI_EVENT_STA_DISCONNECTED; assert(id > 0 && id < 7); handlers[id] = handler; return ESP_OK; }
#include "firmware/components/okou_agent/src/okou_net.c"

okou_config_t g_okou_cfg = OKOU_CONFIG_DEFAULT();
static unsigned wakes, disconnects, got_ips;
void okou_agent_notify(void) { ++wakes; }
void okou_wifi_on_started(void) { }
void okou_wifi_on_disconnected(int reason) { assert(reason == 7); ++disconnects; }
void okou_wifi_on_got_ip(void) { ++got_ips; }
void okou_wifi_reload(void) { }
esp_err_t okou_wifi_init(void) { return ESP_OK; }
int okou_wifi_count(void) { return 0; }
esp_err_t okou_nvs_get_str(const char *key, char *out, size_t len) { out[0] = 0; return ESP_ERR_NOT_FOUND; }

int main(void)
{
    assert(okou_net_start(true) == ESP_OK);
    ip_event_got_ip_t ip = { .ip_info.ip.addr = 42 };
    wifi_event_sta_disconnected_t lost = { .reason = 7 };
    assert(handlers[IP_EVENT_STA_GOT_IP] && handlers[IP_EVENT_ETH_GOT_IP]);
    assert(handlers[IP_EVENT_STA_LOST_IP] && handlers[IP_EVENT_ETH_LOST_IP]);
    assert(handlers[WIFI_EVENT_STA_DISCONNECTED]);
    handlers[IP_EVENT_STA_GOT_IP](NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
    assert(okou_net_has_ip() && wakes == 1 && got_ips == 1);
    handlers[WIFI_EVENT_STA_DISCONNECTED](NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &lost);
    assert(!okou_net_has_ip() && wakes == 2 && disconnects == 1 && !s_ip[0]);
    handlers[IP_EVENT_STA_GOT_IP](NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
    handlers[IP_EVENT_STA_LOST_IP](NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
    assert(!okou_net_has_ip() && wakes == 4 && !s_ip[0]);
    handlers[IP_EVENT_ETH_GOT_IP](NULL, IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip);
    handlers[IP_EVENT_ETH_LOST_IP](NULL, IP_EVENT, IP_EVENT_ETH_LOST_IP, NULL);
    assert(!okou_net_has_ip() && wakes == 6 && !s_ip[0]);
    puts("native network loss wake callbacks: ok");
}
