/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

#define OKOU_HUB_HOST_MAX 99

#ifdef __cplusplus
extern "C" {
#endif
/* ASCII hostname stored in okl/hub. Each label is 1–63 bytes. */
bool okou_hub_host_valid(const char *host);
#ifdef __cplusplus
}
#endif
