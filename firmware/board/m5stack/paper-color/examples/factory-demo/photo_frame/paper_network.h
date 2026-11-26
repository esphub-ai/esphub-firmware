// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t paper_hotspot_ipv4(void);
#ifdef __cplusplus
}
#include "eh_agent_ext.h"

namespace paper_network {
esp_err_t open_hotspot(const char *ssid);
esp_err_t use_station();
uint32_t station_ipv4();
bool socket_on_wifi(int fd, bool hotspot);
int hotspot_clients();  // negative when the local setup hotspot is closed
int scan(eh_ap_t *results, int capacity, uint32_t timeout_ms);
}
#endif
