#pragma once
#include "sdkconfig.h"
#if CONFIG_EH_BLE_WIFI
#include "esp_err.h"
#include <stdbool.h>
void eh_ble_init(void);
esp_err_t eh_ble_install(const char *hex);
#endif
