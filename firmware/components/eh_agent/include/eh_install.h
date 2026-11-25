/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#define EH_BL_IMAGE_PREFIX "EH/bootloader/v1/"

esp_err_t install_op(const cJSON *args, cJSON *data, char *err, size_t errlen);
void install_reconcile_boot(void);
