#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_netif esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
enum { ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI = 114 };
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *address, char *out, int capacity);
esp_err_t esp_netif_dhcps_option(esp_netif_t *netif, int mode, int option, void *data, uint32_t length);
