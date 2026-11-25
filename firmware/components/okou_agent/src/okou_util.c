/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "soc/soc_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "okou_priv.h"
#include "esp_heap_caps.h"

/* Length of a valid UTF-8 sequence starting at s (max n bytes), or 0 if invalid. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    size_t l;
    if (c < 0x80) {
        return 1;
    } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
        l = 2;
    } else if ((c & 0xF0) == 0xE0) {
        l = 3;
    } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
        l = 4;
    } else {
        return 0;
    }
    if (l > n) {
        return 0;
    }
    for (size_t i = 1; i < l; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return l;
}

void okou_utf8_sanitize(char *str)
{
    unsigned char *s = (unsigned char *)str;
    size_t n = strlen(str);
    for (size_t i = 0; i < n;) {
        size_t l = utf8_len(s + i, n - i);
        if (l == 0) {
            s[i] = '?';
            l = 1;
        }
        i += l;
    }
}

const char *okou_reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "ESP_RST_POWERON";
    case ESP_RST_EXT: return "ESP_RST_EXT";
    case ESP_RST_SW: return "ESP_RST_SW";
    case ESP_RST_PANIC: return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT: return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT: return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT: return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP: return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO: return "ESP_RST_SDIO";
    case ESP_RST_USB: return "ESP_RST_USB";
    case ESP_RST_JTAG: return "ESP_RST_JTAG";
    case ESP_RST_EFUSE: return "ESP_RST_EFUSE";
    case ESP_RST_PWR_GLITCH: return "ESP_RST_PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "ESP_RST_CPU_LOCKUP";
    default: return "ESP_RST_UNKNOWN";
    }
}

const char *okou_ota_state_str(const esp_partition_t *p)
{
    if (p->type != ESP_PARTITION_TYPE_APP) {
        return "n/a";
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) {
        return "empty";
    }
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        return "factory";
    }
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(p, &st) != ESP_OK) {
        return "unset"; /* image present, no otadata entry (e.g. flashed over USB) */
    }
    switch (st) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

static const char *subtype_str(const esp_partition_t *p)
{
    static char buf[12];
    if (p->type == ESP_PARTITION_TYPE_APP) {
        if (p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            return "factory";
        }
        if (p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && p->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            snprintf(buf, sizeof(buf), "ota_%d", p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN);
            return buf;
        }
        return "app";
    }
    switch (p->subtype) {
    case ESP_PARTITION_SUBTYPE_DATA_OTA: return "ota";
    case ESP_PARTITION_SUBTYPE_DATA_PHY: return "phy";
    case ESP_PARTITION_SUBTYPE_DATA_NVS: return "nvs";
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "spiffs";
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS: return "littlefs";
    case ESP_PARTITION_SUBTYPE_DATA_FAT: return "fat";
    default: snprintf(buf, sizeof(buf), "0x%02x", (unsigned)(p->subtype & 0xff)); return buf;
    }
}

cJSON *okou_partitions_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "label", p->label);
        cJSON_AddStringToObject(o, "subtype", subtype_str(p));
        cJSON_AddNumberToObject(o, "offset", p->address);
        cJSON_AddNumberToObject(o, "size", p->size);
        cJSON_AddStringToObject(o, "state", okou_ota_state_str(p));
        cJSON_AddItemToArray(arr, o);
    }
    esp_partition_iterator_release(it);
    return arr;
}

bool okou_ble_address(char out[18])
{
#if SOC_BLE_SUPPORTED
    uint8_t m[6];
    if (esp_read_mac(m, ESP_MAC_BT) == ESP_OK) {
        snprintf(out, 18, MACSTR, MAC2STR(m));
        return true;
    }
#endif
    return false;
}

void okou_hex(const uint8_t *in, size_t n, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hex[in[i] >> 4];
        out[2 * i + 1] = hex[in[i] & 0xf];
    }
    out[2 * n] = '\0';
}

/* Large SDK buffers go to PSRAM when the app has it, leaving internal RAM for the BT/Wi-Fi
 * controllers and DMA (coffee-watch's BLE init failed when the SDK took ~30 KB internal RAM). */
void *okou_malloc_large(size_t size)
{
#if CONFIG_SPIRAM
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        return p;
    }
#endif
    return malloc(size);
}

/* ---- NVS helpers (namespace okl) ---- */
esp_err_t okou_nvs_init(void)
{
    static bool done;
    if (done) {
        return ESP_OK;
    }
    /* Never erase: the NVS partition is shared with the app and holds its settings
     * (Wi-Fi, accounts, calibration). An unusable NVS is reported, not wiped; recovering it
     * is a deliberate USB operation that can back the data up first. */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_INVALID_STATE) { /* already initialised by the app */
        e = ESP_OK;
    }
    done = (e == ESP_OK);
    return e;
}

esp_err_t okou_nvs_get_str(const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(OKOU_NVS_NS, NVS_READONLY, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return e;
}

char *okou_nvs_get_str_alloc(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(OKOU_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return NULL;
    }
    size_t len = 0;
    char *out = NULL;
    if (nvs_get_str(h, key, NULL, &len) == ESP_OK && len > 0) {
        out = malloc(len);
        if (out && nvs_get_str(h, key, out, &len) != ESP_OK) {
            free(out);
            out = NULL;
        }
    }
    nvs_close(h);
    return out;
}

esp_err_t okou_nvs_set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(OKOU_NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_set_str(h, key, val);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

uint8_t okou_nvs_get_u8(const char *key, uint8_t def)
{
    nvs_handle_t h;
    uint8_t v = def;
    if (nvs_open(OKOU_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, key, &v) != ESP_OK) {
            v = def;
        }
        nvs_close(h);
    }
    return v;
}

esp_err_t okou_nvs_set_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(OKOU_NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    uint8_t cur;
    if (nvs_get_u8(h, key, &cur) == ESP_OK && cur == val) {
        nvs_close(h);
        return ESP_OK; /* avoid flash writes */
    }
    e = nvs_set_u8(h, key, val);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e;
}

esp_err_t okou_nvs_erase(const char *key)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(OKOU_NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        return e;
    }
    e = nvs_erase_key(h, key);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    } else if (e == ESP_ERR_NVS_NOT_FOUND) {
        e = ESP_OK;
    }
    nvs_close(h);
    return e;
}

const esp_partition_t *okou_bootloader_partition(void)
{
    /* The runtime returns to the last bootloader that passed startup. */
    char label[17] = "";
    if (okou_nvs_get_str(OKOU_KEY_LSLOT, label, sizeof(label)) == ESP_OK &&
        (!strcmp(label, "launcher_a") || !strcmp(label, "launcher_b"))) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    }
    return NULL;
}
