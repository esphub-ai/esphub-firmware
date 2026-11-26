#pragma once
#include "esp_err.h"
typedef struct fake_netif esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(void *netif);
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *name);
