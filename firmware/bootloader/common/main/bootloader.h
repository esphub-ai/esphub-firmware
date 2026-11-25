/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "cJSON.h"

#include "okou_install.h"

void coredump_ops_register(void);

/* Store a provisioning JSON object (BLE bootstrap key/hub/token/name) in NVS namespace okl. */
bool prov_apply(const char *line, char *err, size_t n);

/* USB-serial provisioning: a console line "OKL-PROVISION <json>" applies the same payload. */
void prov_usb_cdc_init(void);
void prov_serial_start(void);
