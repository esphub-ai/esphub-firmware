/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "cJSON.h"

#include "eh_install.h"

esp_err_t resize_app(const cJSON *args, cJSON *result, char *err, size_t errlen);
void report_layout(void);
bool current_layout(uint32_t *app_size, uint32_t *data_size);
bool launcher_ota_busy(void);
void launcher_ota_set_busy(bool busy);


/* Store a provisioning JSON object (BLE bootstrap key/hub/token/name) in NVS namespace eh. */
bool prov_apply(const char *line, char *err, size_t n);

/* USB-serial provisioning: a console line "EH-PROVISION <json>" applies the same payload. */
void prov_serial_start(void);
