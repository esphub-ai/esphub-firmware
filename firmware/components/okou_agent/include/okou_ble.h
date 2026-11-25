#pragma once
#include "sdkconfig.h"
#if CONFIG_OKOU_BLE_WIFI
#include "esp_err.h"
#include <stdbool.h>
void okou_ble_init(void);
esp_err_t okou_ble_install(const char *hex);
bool okou_ble_host_owned(void);
#endif
