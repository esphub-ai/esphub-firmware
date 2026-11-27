/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "cJSON.h"

#include "eh_install.h"

esp_err_t resize_app(const cJSON *args, cJSON *result, char *err, size_t errlen);
void report_layout(void);
esp_err_t launcher_install(const cJSON *args, cJSON *result, char *err, size_t errlen);
void reconcile_resize(void);

/* Local USB console for Launcher commands and EH-WIFI Wi-Fi setup. */
void usb_console_start(void);
