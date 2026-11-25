/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "eh_agent.h"
#include "eh_agent_ext.h"

#define EH_TAG "okou"

/* ---- registry (eh_registry.c) ---- */
typedef enum { EH_P_INT, EH_P_FLOAT, EH_P_BOOL } eh_ptype_t;
void eh_registry_builtin_cmds(void);
cJSON *eh_cmd_list_json(void);
/* Runs a command; returns malloc'd UTF-8 output via *out_text; rc via return. -1000: unknown */
int eh_cmd_run(const char *name, const cJSON *argv, char **out_text);
cJSON *eh_param_list_json(void);
esp_err_t eh_param_get_json(const char *name, cJSON *data, char *err, size_t errlen);
esp_err_t eh_param_set_json(const cJSON *args, cJSON *data, char *err, size_t errlen);

/* ---- network (eh_net.c) ---- */
void *eh_malloc_large(size_t size);
esp_err_t eh_net_start(bool manage);
esp_err_t eh_wifi_init(void);
void eh_wifi_on_started(void);
void eh_wifi_on_got_ip(void);
void eh_wifi_on_disconnected(int reason);
void eh_wifi_reload(void);
void eh_net_reconfigure(void);
void eh_net_info(cJSON *obj); /* adds ip, rssi */
int eh_net_describe(char *buf, size_t len);

/* ---- util (eh_util.c) ---- */
void eh_utf8_sanitize(char *s);
const char *eh_reset_reason_str(void);
const char *eh_ota_state_str(const esp_partition_t *p);
void eh_hex(const uint8_t *in, size_t n, char *out);
cJSON *eh_partitions_json(void);

/* ---- agent (eh_agent.c) ---- */
void eh_agent_notify(void);
/* The agent task owns the console session and its send schedule. */
esp_err_t eh_console_set(bool enabled);
int64_t eh_console_deadline(void);
void eh_console_flush(void);
extern eh_config_t g_eh_cfg;

static inline bool eh_is_bootloader(void)
{
#ifdef EH_BOOTLOADER_BUILD
    return true;
#else
    return g_eh_cfg.role == EH_ROLE_BOOTLOADER;
#endif
}
