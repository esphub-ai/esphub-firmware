/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "cJSON.h"

#include "eh_install.h"


/* Store a provisioning JSON object (BLE bootstrap key/hub/token/name) in NVS namespace eh. */
bool prov_apply(const char *line, char *err, size_t n);

/* USB-serial provisioning: a console line "EH-PROVISION <json>" applies the same payload. */
void prov_serial_start(void);
