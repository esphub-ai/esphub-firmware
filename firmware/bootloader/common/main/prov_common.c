/*
 * Parse and store the local USB serial provisioning record.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "eh_agent_ext.h"
#include "bootloader.h"
#include "eh_ble.h"

bool prov_apply(const char *line, char *err, size_t n)
{
    cJSON *j = cJSON_Parse(line);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        snprintf(err, n, "invalid JSON");
        return false;
    }
    const cJSON *hub_field = cJSON_GetObjectItemCaseSensitive(j, "hub");
    const cJSON *tok = cJSON_GetObjectItemCaseSensitive(j, "token");
    const char *hub = cJSON_GetStringValue(hub_field);
    const char *dtok = cJSON_GetStringValue(tok);
    bool ok = true;
    if (hub_field && !eh_hub_host_valid(hub)) {
        snprintf(err, n, "hub must be a hostname such as api.esphub.ai");
        ok = false;
    } else if (tok && (!dtok || strncmp(dtok, "eh_dev_", sizeof("eh_dev_") - 1) || strlen(dtok) > 95)) {
        snprintf(err, n, "token must be a hub device token (eh_dev_...)");
        ok = false;
    }
#if CONFIG_EH_BLE_WIFI
    const cJSON *ble = cJSON_GetObjectItemCaseSensitive(j, "ble_secret");
    if (ok && ble && (!dtok || !cJSON_IsString(ble) || strlen(ble->valuestring) != 64 ||
                      strspn(ble->valuestring, "0123456789abcdef") != 64)) {
        snprintf(err, n, "invalid BLE bootstrap");
        ok = false;
    }
    if (ok && dtok) {
        char previous[96];
        if (eh_nvs_get_str(EH_KEY_TOKEN, previous, sizeof(previous)) != ESP_OK || strcmp(dtok, previous)) {
            ok = eh_nvs_erase("ble_key") == ESP_OK;
            if (!ok) snprintf(err, n, "BLE credential reset failed");
        }
    }
#endif
    if (ok && dtok && !hub) hub = "api.esphub.ai";
    if (ok) {
        const char *keys[] = {"hub", EH_KEY_TOKEN};
        const char *vals[] = {hub, dtok};
        for (int i = 0; i < 2 && ok; i++) {
            if (vals[i] && eh_nvs_set_str(keys[i], vals[i]) != ESP_OK) {
                snprintf(err, n, "NVS write failed (%s)", keys[i]);
                ok = false;
            }
        }
    }
#if CONFIG_EH_BLE_WIFI
    if (ok && ble && eh_ble_install(ble->valuestring) != ESP_OK) {
        snprintf(err, n, "invalid BLE bootstrap or NVS write failed");
        ok = false;
    }
#endif
    cJSON_Delete(j);
    return ok;
}
