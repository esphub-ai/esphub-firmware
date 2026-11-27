/* Command registry and built-in commands. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"
#include "eh_priv.h"
#include "eh_agent_ext.h"

#define MAX_ARGS 16

struct eh_out {
    char *buf;
    size_t len;
    size_t cap;
};

typedef struct {
    char *name;
    char *help;
    eh_cmd_handler_t fn;
} cmd_t;

static cmd_t s_cmds[CONFIG_EH_MAX_CMDS];
static int s_ncmds;

int eh_outf(eh_out_t *o, const char *fmt, ...)
{
    if (!o || !o->buf || o->len + 1 >= o->cap) {
        return 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + o->len, o->cap - o->len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return n;
    }
    if (o->len + (size_t)n >= o->cap) {
        o->len = o->cap - 1;
    } else {
        o->len += (size_t)n;
    }
    return n;
}

esp_err_t eh_cmd_register(const char *name, const char *help, eh_cmd_handler_t handler)
{
    if (!name || !*name || !handler || strlen(name) > 23) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) {
            s_cmds[i].fn = handler; /* re-register replaces */
            return ESP_OK;
        }
    }
    if (s_ncmds >= CONFIG_EH_MAX_CMDS) {
        return ESP_ERR_NO_MEM;
    }
    s_cmds[s_ncmds].name = strdup(name);
    s_cmds[s_ncmds].help = strdup(help ? help : "");
    s_cmds[s_ncmds].fn = handler;
    s_ncmds++;
    return ESP_OK;
}

int eh_cmd_run(const char *name, const cJSON *argv_json, char **out_text)
{
    *out_text = NULL;
    cmd_t *c = NULL;
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) {
            c = &s_cmds[i];
            break;
        }
    }
    if (!c) {
        return -1000;
    }
    char *argv[MAX_ARGS + 1] = {0};
    int argc = 0;
    argv[argc++] = (char *)c->name;
    const cJSON *it;
    cJSON_ArrayForEach(it, argv_json) {
        if (argc >= MAX_ARGS) {
            break;
        }
        char tmp[32];
        if (cJSON_IsString(it)) {
            argv[argc++] = strdup(it->valuestring);
        } else if (cJSON_IsNumber(it)) {
            snprintf(tmp, sizeof(tmp), "%.15g", it->valuedouble);
            argv[argc++] = strdup(tmp);
        } else if (cJSON_IsBool(it)) {
            argv[argc++] = strdup(cJSON_IsTrue(it) ? "true" : "false");
        }
    }
    eh_out_t out = {.buf = malloc(CONFIG_EH_CMD_OUT_MAX), .len = 0, .cap = CONFIG_EH_CMD_OUT_MAX};
    if (out.buf) {
        out.buf[0] = '\0';
    }
    int rc = c->fn(argc, argv, &out);
    for (int i = 1; i < argc; i++) {
        free(argv[i]);
    }
    if (out.buf) {
        out.buf[out.len] = '\0';
        eh_utf8_sanitize(out.buf);
    }
    *out_text = out.buf;
    return rc;
}

/* ---- built-in commands ---- */

static int cmd_help(int argc, char **argv, eh_out_t *out)
{
    for (int i = 0; i < s_ncmds; i++) {
        eh_outf(out, "%-12s %s\n", s_cmds[i].name, s_cmds[i].help);
    }
    return 0;
}

static int cmd_version(int argc, char **argv, eh_out_t *out)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[65];
    eh_hex(d->app_elf_sha256, 32, sha);
    const esp_partition_t *run = esp_ota_get_running_partition();
    eh_outf(out, "%s %s (idf %s)\nrole %s, partition %s (%s)\nelf sha256 %s\nbuilt %s %s\n", d->project_name,
              d->version, d->idf_ver, eh_is_bootloader() ? "bootloader" : "app",
              run ? run->label : "?", run ? eh_ota_state_str(run) : "?", sha, d->date, d->time);
    return 0;
}

static int cmd_heap(int argc, char **argv, eh_out_t *out)
{
    eh_outf(out, "free %u min_free %u internal_free %u internal_min_free %u largest_internal %u\n",
              (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    size_t ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (ps) {
        eh_outf(out, "psram_free %u\n", (unsigned)ps);
    }
    return 0;
}

static int cmd_wifi(int argc, char **argv, eh_out_t *out)
{
    char buf[160];
    if (argc < 2) {
        eh_net_describe(buf, sizeof(buf));
        eh_outf(out, "%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        cJSON *arr = cJSON_CreateArray();
        eh_wifi_saved_json(arr);
        cJSON *o;
        int i = 0;
        cJSON_ArrayForEach(o, arr)
        {
            eh_outf(out, "%d. %s%s%s%s\n", ++i, cJSON_GetObjectItem(o, "ssid")->valuestring,
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "hidden")) ? " [hidden]" : "",
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "open")) ? " [open]" : "",
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "current")) ? " <- connected" : "");
        }
        cJSON_Delete(arr);
        if (!i) {
            eh_outf(out, "no saved networks\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "scan") == 0) {
        eh_ap_t aps[20];
        int n = eh_wifi_scan(aps, 20, 20000);
        if (n < 0) {
            eh_outf(out, "scan failed (busy or not available)\n");
            return 1;
        }
        for (int i = 0; i < n; i++) {
            eh_outf(out, "%4d dBm  ch%-2u  %-9s  %s\n", aps[i].rssi, aps[i].channel, eh_wifi_auth_str(aps[i].auth),
                      aps[i].ssid);
        }
        eh_outf(out, "%d networks\n", n);
        return 0;
    }
    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        bool hidden = argc >= 5 && strcmp(argv[4], "hidden") == 0;
        esp_err_t e = eh_wifi_add(argv[2], argc >= 4 ? argv[3] : "", hidden);
        eh_outf(out, e == ESP_OK ? "saved %s%s\n" : "failed to save %s%s\n", argv[2], hidden ? " (hidden)" : "");
        return e == ESP_OK ? 0 : 1;
    }
    if (strcmp(argv[1], "forget") == 0 && argc >= 3) {
        esp_err_t e = eh_wifi_forget(argv[2]);
        eh_outf(out, e == ESP_OK ? "forgot %s\n" : "%s is not saved\n", argv[2]);
        return e == ESP_OK ? 0 : 1;
    }
    eh_outf(out, "usage: wifi [list | scan | add <ssid> [pass] [hidden] | forget <ssid>]\n");
    return 1;
}

static void builtin(const char *name, const char *help, eh_cmd_handler_t fn)
{
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) {
            return; /* the app registered its own version first */
        }
    }
    eh_cmd_register(name, help, fn);
}

void eh_registry_builtin_cmds(void)
{
    builtin("help", "list commands", cmd_help);
    builtin("version", "firmware version and running partition", cmd_version);
    builtin("heap", "heap statistics", cmd_heap);
    builtin("wifi", "wifi [list|scan|add <ssid> [pass] [hidden]|forget <ssid>]: status and saved networks", cmd_wifi);
}
