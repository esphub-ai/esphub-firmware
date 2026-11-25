/* Command and param registries. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_idf_version.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "okou_priv.h"
#include "okou_agent_ext.h"

#define PARAM_NS "okp"
#define MAX_ARGS 16

struct okou_out {
    char *buf;
    size_t len;
    size_t cap;
};

typedef struct {
    char *name;
    char *help;
    okou_cmd_handler_t fn;
} cmd_t;

typedef struct {
    char name[24];
    okou_ptype_t type;
    void *ptr;
    double min, max;
    bool persist;
} param_t;

static cmd_t s_cmds[CONFIG_OKOU_MAX_CMDS];
static int s_ncmds;
static param_t s_params[CONFIG_OKOU_MAX_PARAMS];
static int s_nparams;

int okou_outf(okou_out_t *o, const char *fmt, ...)
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

esp_err_t okou_cmd_register(const char *name, const char *help, okou_cmd_handler_t handler)
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
    if (s_ncmds >= CONFIG_OKOU_MAX_CMDS) {
        return ESP_ERR_NO_MEM;
    }
    s_cmds[s_ncmds].name = strdup(name);
    s_cmds[s_ncmds].help = strdup(help ? help : "");
    s_cmds[s_ncmds].fn = handler;
    s_ncmds++;
    return ESP_OK;
}

cJSON *okou_cmd_list_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_ncmds; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", s_cmds[i].name);
        cJSON_AddStringToObject(o, "help", s_cmds[i].help);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

int okou_cmd_run(const char *name, const cJSON *argv_json, char **out_text)
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
    okou_out_t out = {.buf = malloc(CONFIG_OKOU_CMD_OUT_MAX), .len = 0, .cap = CONFIG_OKOU_CMD_OUT_MAX};
    if (out.buf) {
        out.buf[0] = '\0';
    }
    int rc = c->fn(argc, argv, &out);
    for (int i = 1; i < argc; i++) {
        free(argv[i]);
    }
    if (out.buf) {
        out.buf[out.len] = '\0';
        okou_utf8_sanitize(out.buf);
    }
    *out_text = out.buf;
    return rc;
}

/* ---- params ---- */

static param_t *param_find(const char *name)
{
    for (int i = 0; i < s_nparams; i++) {
        if (strcmp(s_params[i].name, name) == 0) {
            return &s_params[i];
        }
    }
    return NULL;
}

static double param_value(const param_t *p)
{
    switch (p->type) {
    case OKOU_P_INT: return *(int32_t *)p->ptr;
    case OKOU_P_FLOAT: return *(float *)p->ptr;
    default: return *(bool *)p->ptr ? 1 : 0;
    }
}

static void param_add_value(cJSON *o, const char *key, const param_t *p)
{
    if (p->type == OKOU_P_BOOL) {
        cJSON_AddBoolToObject(o, key, *(bool *)p->ptr);
    } else {
        cJSON_AddNumberToObject(o, key, param_value(p));
    }
}

static esp_err_t param_save(const param_t *p)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(PARAM_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    switch (p->type) {
    case OKOU_P_INT: e = nvs_set_i32(h, p->name, *(int32_t *)p->ptr); break;
    case OKOU_P_FLOAT: {
        uint32_t bits;
        memcpy(&bits, p->ptr, 4);
        e = nvs_set_u32(h, p->name, bits);
        break;
    }
    default: e = nvs_set_u8(h, p->name, *(bool *)p->ptr ? 1 : 0); break;
    }
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

static void param_load(param_t *p)
{
    nvs_handle_t h;
    if (nvs_open(PARAM_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    switch (p->type) {
    case OKOU_P_INT: {
        int32_t v;
        if (nvs_get_i32(h, p->name, &v) == ESP_OK && v >= p->min && v <= p->max) {
            *(int32_t *)p->ptr = v;
        }
        break;
    }
    case OKOU_P_FLOAT: {
        uint32_t bits;
        if (nvs_get_u32(h, p->name, &bits) == ESP_OK) {
            float f;
            memcpy(&f, &bits, 4);
            if (isfinite(f) && f >= p->min && f <= p->max) {
                *(float *)p->ptr = f;
            }
        }
        break;
    }
    default: {
        uint8_t v;
        if (nvs_get_u8(h, p->name, &v) == ESP_OK) {
            *(bool *)p->ptr = v != 0;
        }
        break;
    }
    }
    nvs_close(h);
}

static esp_err_t param_add(const char *name, okou_ptype_t t, void *ptr, double min, double max, bool persist)
{
    if (!name || !ptr || strlen(name) >= sizeof(s_params[0].name) || min > max) {
        return ESP_ERR_INVALID_ARG;
    }
    if (persist && strlen(name) > NVS_KEY_NAME_MAX_SIZE - 1) {
        ESP_LOGE(OKOU_TAG, "param %s: name too long to persist (max 15)", name);
        return ESP_ERR_INVALID_ARG;
    }
    if (param_find(name)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_nparams >= CONFIG_OKOU_MAX_PARAMS) {
        return ESP_ERR_NO_MEM;
    }
    okou_nvs_init();
    param_t *p = &s_params[s_nparams];
    strlcpy(p->name, name, sizeof(p->name));
    p->type = t;
    p->ptr = ptr;
    p->min = min;
    p->max = max;
    p->persist = persist;
    if (persist) {
        param_load(p);
    }
    s_nparams++;
    return ESP_OK;
}

esp_err_t okou_param_int(const char *name, int32_t *ptr, int32_t min, int32_t max, bool persist)
{
    return param_add(name, OKOU_P_INT, ptr, min, max, persist);
}

esp_err_t okou_param_float(const char *name, float *ptr, float min, float max, bool persist)
{
    return param_add(name, OKOU_P_FLOAT, ptr, min, max, persist);
}

esp_err_t okou_param_bool(const char *name, bool *ptr, bool persist)
{
    return param_add(name, OKOU_P_BOOL, ptr, 0, 1, persist);
}

esp_err_t okou_param_save(const char *name)
{
    param_t *p = name ? param_find(name) : NULL;
    if (!p) {
        return ESP_ERR_NOT_FOUND;
    }
    return p->persist ? param_save(p) : ESP_ERR_INVALID_STATE;
}

static const char *type_str(okou_ptype_t t)
{
    return t == OKOU_P_INT ? "int" : (t == OKOU_P_FLOAT ? "float" : "bool");
}

cJSON *okou_param_list_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_nparams; i++) {
        param_t *p = &s_params[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", p->name);
        cJSON_AddStringToObject(o, "type", type_str(p->type));
        param_add_value(o, "value", p);
        cJSON_AddNumberToObject(o, "min", p->min);
        cJSON_AddNumberToObject(o, "max", p->max);
        cJSON_AddBoolToObject(o, "persist", p->persist);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

esp_err_t okou_param_get_json(const char *name, cJSON *data, char *err, size_t errlen)
{
    param_t *p = name ? param_find(name) : NULL;
    if (!p) {
        snprintf(err, errlen, "unknown param: %s", name ? name : "(none)");
        return ESP_ERR_NOT_FOUND;
    }
    cJSON_AddStringToObject(data, "name", p->name);
    param_add_value(data, "value", p);
    return ESP_OK;
}

esp_err_t okou_param_set_json(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    const cJSON *jn = cJSON_GetObjectItemCaseSensitive(args, "name");
    const cJSON *jv = cJSON_GetObjectItemCaseSensitive(args, "value");
    const cJSON *jp = cJSON_GetObjectItemCaseSensitive(args, "persist");
    if (!cJSON_IsString(jn)) {
        snprintf(err, errlen, "missing name");
        return ESP_ERR_INVALID_ARG;
    }
    param_t *p = param_find(jn->valuestring);
    if (!p) {
        snprintf(err, errlen, "unknown param: %s", jn->valuestring);
        return ESP_ERR_NOT_FOUND;
    }
    double v;
    if (cJSON_IsBool(jv)) {
        v = cJSON_IsTrue(jv) ? 1 : 0;
        if (p->type != OKOU_P_BOOL) {
            snprintf(err, errlen, "%s expects a number", p->name);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (cJSON_IsNumber(jv)) {
        v = jv->valuedouble;
    } else {
        snprintf(err, errlen, "missing or non-numeric value");
        return ESP_ERR_INVALID_ARG;
    }
    if (!isfinite(v)) {
        snprintf(err, errlen, "value must be finite");
        return ESP_ERR_INVALID_ARG;
    }
    if (p->type == OKOU_P_INT && v != floor(v)) {
        snprintf(err, errlen, "%s expects an integer", p->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (p->type == OKOU_P_BOOL && v != 0 && v != 1) {
        snprintf(err, errlen, "%s expects true/false", p->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (v < p->min || v > p->max) {
        snprintf(err, errlen, "%s out of range [%g, %g]", p->name, p->min, p->max);
        return ESP_ERR_INVALID_ARG;
    }
    switch (p->type) {
    case OKOU_P_INT: *(volatile int32_t *)p->ptr = (int32_t)v; break;
    case OKOU_P_FLOAT: *(volatile float *)p->ptr = (float)v; break;
    default: *(volatile bool *)p->ptr = v != 0; break;
    }
    bool persist = p->persist && (!jp || cJSON_IsTrue(jp));
    if (persist) {
        esp_err_t e = param_save(p);
        if (e != ESP_OK) {
            snprintf(err, errlen, "value set but NVS save failed: %s", esp_err_to_name(e));
            return e;
        }
    }
    ESP_LOGI(OKOU_TAG, "param %s = %g%s", p->name, v, persist ? " (saved)" : "");
    cJSON_AddStringToObject(data, "name", p->name);
    param_add_value(data, "value", p);
    return ESP_OK;
}

/* ---- built-in commands ---- */

static int cmd_help(int argc, char **argv, okou_out_t *out)
{
    for (int i = 0; i < s_ncmds; i++) {
        okou_outf(out, "%-12s %s\n", s_cmds[i].name, s_cmds[i].help);
    }
    return 0;
}

static int cmd_version(int argc, char **argv, okou_out_t *out)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[65];
    okou_hex(d->app_elf_sha256, 32, sha);
    const esp_partition_t *run = esp_ota_get_running_partition();
    okou_outf(out, "%s %s (idf %s)\nrole %s, partition %s (%s)\nelf sha256 %s\nbuilt %s %s\n", d->project_name,
              d->version, d->idf_ver, okou_is_bootloader() ? "bootloader" : "app",
              run ? run->label : "?", run ? okou_ota_state_str(run) : "?", sha, d->date, d->time);
    return 0;
}

static int cmd_heap(int argc, char **argv, okou_out_t *out)
{
    okou_outf(out, "free %u min_free %u internal_free %u internal_min_free %u largest_internal %u\n",
              (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    size_t ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (ps) {
        okou_outf(out, "psram_free %u\n", (unsigned)ps);
    }
    return 0;
}

static int cmd_wifi(int argc, char **argv, okou_out_t *out)
{
    char buf[160];
    if (argc < 2) {
        okou_net_describe(buf, sizeof(buf));
        okou_outf(out, "%s\n", buf);
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        cJSON *arr = cJSON_CreateArray();
        okou_wifi_saved_json(arr);
        cJSON *o;
        int i = 0;
        cJSON_ArrayForEach(o, arr)
        {
            okou_outf(out, "%d. %s%s%s%s\n", ++i, cJSON_GetObjectItem(o, "ssid")->valuestring,
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "hidden")) ? " [hidden]" : "",
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "open")) ? " [open]" : "",
                      cJSON_IsTrue(cJSON_GetObjectItem(o, "current")) ? " <- connected" : "");
        }
        cJSON_Delete(arr);
        if (!i) {
            okou_outf(out, "no saved networks\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "scan") == 0) {
        okou_ap_t aps[20];
        int n = okou_wifi_scan(aps, 20, 20000);
        if (n < 0) {
            okou_outf(out, "scan failed (busy or not available)\n");
            return 1;
        }
        for (int i = 0; i < n; i++) {
            okou_outf(out, "%4d dBm  ch%-2u  %-9s  %s\n", aps[i].rssi, aps[i].channel, okou_wifi_auth_str(aps[i].auth),
                      aps[i].ssid);
        }
        okou_outf(out, "%d networks\n", n);
        return 0;
    }
    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        bool hidden = argc >= 5 && strcmp(argv[4], "hidden") == 0;
        esp_err_t e = okou_wifi_add(argv[2], argc >= 4 ? argv[3] : "", hidden);
        okou_outf(out, e == ESP_OK ? "saved %s%s\n" : "failed to save %s%s\n", argv[2], hidden ? " (hidden)" : "");
        return e == ESP_OK ? 0 : 1;
    }
    if (strcmp(argv[1], "forget") == 0 && argc >= 3) {
        esp_err_t e = okou_wifi_forget(argv[2]);
        okou_outf(out, e == ESP_OK ? "forgot %s\n" : "%s is not saved\n", argv[2]);
        return e == ESP_OK ? 0 : 1;
    }
    okou_outf(out, "usage: wifi [list | scan | add <ssid> [pass] [hidden] | forget <ssid>]\n");
    return 1;
}

static void builtin(const char *name, const char *help, okou_cmd_handler_t fn)
{
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, name) == 0) {
            return; /* the app registered its own version first */
        }
    }
    okou_cmd_register(name, help, fn);
}

void okou_registry_builtin_cmds(void)
{
    builtin("help", "list commands", cmd_help);
    builtin("version", "firmware version and running partition", cmd_version);
    builtin("heap", "heap statistics", cmd_heap);
    builtin("wifi", "wifi [list|scan|add <ssid> [pass] [hidden]|forget <ssid>]: status and saved networks", cmd_wifi);
}
