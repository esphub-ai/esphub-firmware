/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#define EH_BL_IMAGE_PREFIX "ESPHUB2/bootloader/"
#define EH_OTA_CAPABILITY "bootloader-ota-v2"

esp_err_t eh_install_op(const cJSON *args, cJSON *data, char *err, size_t errlen);
void eh_install_reconcile_boot(void);
