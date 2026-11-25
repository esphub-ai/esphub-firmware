/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#define OKOU_BL_IMAGE_PREFIX "ESPHUB2/bootloader/"
#define OKOU_MICROPYTHON_IMAGE_PREFIX "ESPHUB2/micropython/"
#define OKOU_OTA_CAPABILITY "bootloader-ota-v2"

esp_err_t install_op(const cJSON *args, cJSON *data, char *err, size_t errlen);
void install_reconcile_boot(void);
