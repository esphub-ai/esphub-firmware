/* Wi-Fi manager for SDK-managed networking (bootloader and apps with manage_network=true).
 *
 *  - Remembers up to EH_WIFI_MAX networks in NVS eh/wifis (JSON, most recently used first).
 *  - Connect loop (own task): scan, try saved networks that are visible (strongest first), then
 *    the rest including hidden ones (direct connect by SSID), back off when nothing works, and
 *    wait for a disconnect once online.
 *  - eh_wifi_scan() is serialised with connection attempts through one radio mutex.
 *
 * SPDX-License-Identifier: MIT */
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "eh_priv.h"
#include "eh_agent_ext.h"

#if !CONFIG_EH_NET_OPENETH
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mbedtls/platform_util.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"

typedef struct {
    char ssid[33];
    char pass[65];
    bool hidden;
} net_t;

#define EV_STARTED BIT0
#define EV_GOT_IP BIT1
#define EV_DISC BIT2
#define EV_RECONF BIT3
#define EV_MANAGED BIT4
#define EV_CANCEL BIT5
#define ATTEMPT_TIMEOUT_MS 15000
#define SCAN_MAX 24

static net_t s_nets[EH_WIFI_MAX];
static int s_count;
static SemaphoreHandle_t s_list_mu;  /* s_nets */
static SemaphoreHandle_t s_radio_mu; /* scan vs connection attempt */
static EventGroupHandle_t s_ev;
static SemaphoreHandle_t s_owner_mu; /* serializes public ownership changes */
static esp_netif_t *s_sta_netif;     /* only set for the SDK-owned driver */
static bool s_task_created;

static bool managed(void) { return s_ev && (xEventGroupGetBits(s_ev) & EV_MANAGED); }
static _Atomic bool s_online;
static char s_current[33];
static _Atomic int s_last_reason;

static void lock(void) { xSemaphoreTake(s_list_mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_list_mu); }

static esp_err_t save_locked(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "s", s_nets[i].ssid);
        cJSON_AddStringToObject(o, "p", s_nets[i].pass);
        if (s_nets[i].hidden) {
            cJSON_AddBoolToObject(o, "h", true);
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *js = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!js) return ESP_ERR_NO_MEM;
    esp_err_t saved = eh_nvs_set_str(EH_KEY_WIFIS, js);
    free(js);
    return saved;
}

static void load(void)
{
    lock();
    s_count = 0;
    char *js = eh_nvs_get_str_alloc(EH_KEY_WIFIS);
    if (js) {
        cJSON *arr = cJSON_Parse(js);
        free(js);
        cJSON *o;
        cJSON_ArrayForEach(o, arr)
        {
            const cJSON *s = cJSON_GetObjectItemCaseSensitive(o, "s");
            const cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "p");
            if (!cJSON_IsString(s) || !s->valuestring[0] || s_count >= EH_WIFI_MAX) {
                continue;
            }
            net_t *n = &s_nets[s_count++];
            memset(n, 0, sizeof(*n));
            strlcpy(n->ssid, s->valuestring, sizeof(n->ssid));
            strlcpy(n->pass, cJSON_IsString(p) ? p->valuestring : "", sizeof(n->pass));
            n->hidden = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "h"));
        }
        cJSON_Delete(arr);
    }
    unlock();
}

int eh_wifi_count(void)
{
    if (!s_list_mu) return 0;
    lock();
    int count = s_count;
    unlock();
    return count;
}

esp_err_t eh_wifi_add(const char *ssid, const char *pass, bool hidden)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (pass && strlen(pass) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_list_mu) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    net_t n = {0};
    strlcpy(n.ssid, ssid, sizeof(n.ssid));
    strlcpy(n.pass, pass ? pass : "", sizeof(n.pass));
    n.hidden = hidden;
    int w = 1;
    net_t kept[EH_WIFI_MAX];
    kept[0] = n;
    for (int i = 0; i < s_count && w < EH_WIFI_MAX; i++) {
        if (strcmp(s_nets[i].ssid, ssid) != 0) {
            kept[w++] = s_nets[i];
        }
    }
    net_t previous[EH_WIFI_MAX];
    memcpy(previous, s_nets, sizeof(previous));
    int previous_count = s_count;
    memcpy(s_nets, kept, sizeof(net_t) * w);
    s_count = w;
    esp_err_t saved = save_locked();
    if (saved != ESP_OK) {
        memcpy(s_nets, previous, sizeof(previous));
        s_count = previous_count;
    }
    unlock();
    if (saved != ESP_OK) return saved;
    ESP_LOGI(EH_TAG, "wifi: saved %s%s (%d networks)", ssid, hidden ? " (hidden)" : "", w);
    xEventGroupSetBits(s_ev, EV_RECONF);
    return ESP_OK;
}

esp_err_t eh_wifi_forget(const char *ssid)
{
    if (!s_list_mu) {
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    int w = 0;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_nets[i].ssid, ssid) != 0) {
            s_nets[w++] = s_nets[i];
        }
    }
    bool found = w != s_count;
    s_count = w;
    if (found) {
        save_locked();
    }
    unlock();
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(EH_TAG, "wifi: forgot %s (%d networks left)", ssid, w);
    xEventGroupSetBits(s_ev, EV_RECONF);
    return ESP_OK;
}

void eh_wifi_saved_json(cJSON *arr)
{
    if (!s_list_mu) {
        return;
    }
    lock();
    for (int i = 0; i < s_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", s_nets[i].ssid);
        cJSON_AddBoolToObject(o, "hidden", s_nets[i].hidden);
        cJSON_AddBoolToObject(o, "open", s_nets[i].pass[0] == '\0');
        cJSON_AddBoolToObject(o, "current", s_online && strcmp(s_current, s_nets[i].ssid) == 0);
        cJSON_AddItemToArray(arr, o);
    }
    unlock();
}

const char *eh_wifi_auth_str(int auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "enterprise";
    default: return "other";
    }
}

static int cmp_rssi(const void *a, const void *b)
{
    return ((const eh_ap_t *)b)->rssi - ((const eh_ap_t *)a)->rssi;
}

/* Caller holds s_radio_mu. Returns visible networks, one entry per SSID (strongest). */
static int scan_locked(eh_ap_t *out, int max)
{
    wifi_scan_config_t sc = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 60, .max = 120},
    };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        return -1;
    }
    uint16_t n = SCAN_MAX;
    wifi_ap_record_t *recs = calloc(SCAN_MAX, sizeof(*recs));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return -1;
    }
    esp_wifi_scan_get_ap_records(&n, recs);
    int m = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) {
            continue;
        }
        int j = 0;
        while (j < m && strcmp(out[j].ssid, ssid) != 0) {
            j++;
        }
        if (j < m) {
            if (recs[i].rssi > out[j].rssi) {
                out[j].rssi = recs[i].rssi;
                out[j].channel = recs[i].primary;
                out[j].auth = recs[i].authmode;
                memcpy(out[j].bssid, recs[i].bssid, sizeof(out[j].bssid));
            }
            continue;
        }
        if (m >= max) {
            continue;
        }
        strlcpy(out[m].ssid, ssid, sizeof(out[m].ssid));
        out[m].rssi = recs[i].rssi;
        out[m].channel = recs[i].primary;
        out[m].auth = recs[i].authmode;
        memcpy(out[m].bssid, recs[i].bssid, sizeof(out[m].bssid));
        m++;
    }
    free(recs);
    qsort(out, m, sizeof(*out), cmp_rssi);
    return m;
}

int eh_wifi_scan(eh_ap_t *out, int max, uint32_t timeout_ms)
{
    if (!s_radio_mu || xSemaphoreTake(s_radio_mu, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -1;
    }
    int n = managed() ? scan_locked(out, max) : -1;
    xSemaphoreGive(s_radio_mu);
    return n;
}

bool eh_wifi_current(char *ssid, size_t len)
{
    if (!s_list_mu) return false;
    lock();
    bool current = managed() && s_online;
    if (current) strlcpy(ssid, s_current, len);
    unlock();
    return current;
}

/* ---- events from eh_net.c ---- */
void eh_wifi_on_started(void) { if (s_ev) xEventGroupSetBits(s_ev, EV_STARTED); }
void eh_wifi_on_got_ip(void) { if (managed()) xEventGroupSetBits(s_ev, EV_GOT_IP); }
void eh_wifi_on_disconnected(int reason)
{
    s_last_reason = reason;
    s_online = false;
    if (s_ev) {
        xEventGroupSetBits(s_ev, EV_DISC);
    }
}

static TickType_t attempt_remaining(TickType_t started)
{
    TickType_t elapsed = xTaskGetTickCount() - started;
    TickType_t budget = pdMS_TO_TICKS(ATTEMPT_TIMEOUT_MS);
    return elapsed < budget ? budget - elapsed : 0;
}

static bool attempt(const net_t *n, const eh_ap_t *hint, TickType_t started)
{
    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, n->ssid, strnlen(n->ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, n->pass, strnlen(n->pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = n->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.scan_method = hint ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
    if (hint) {
        wc.sta.channel = hint->channel;
        wc.sta.bssid_set = true;
        memcpy(wc.sta.bssid, hint->bssid, sizeof(wc.sta.bssid));
    } /* Unhinted attempts also find hidden SSIDs via directed probes. */
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (!managed() || esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        return false;
    }
    xEventGroupClearBits(s_ev, EV_GOT_IP | EV_DISC);
    ESP_LOGI(EH_TAG, "wifi: trying %s%s", n->ssid, n->hidden ? " (hidden)" : "");
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }
    EventBits_t b = xEventGroupWaitBits(s_ev, EV_GOT_IP | EV_DISC | EV_CANCEL, pdTRUE, pdFALSE, attempt_remaining(started));
    if (!managed()) return false;
    if (b & EV_GOT_IP) {
        lock();
        strlcpy(s_current, n->ssid, sizeof(s_current));
        s_online = true;
        unlock();
        return true;
    }
    ESP_LOGW(EH_TAG, "wifi: %s failed (%s reason %d)", n->ssid, (b & EV_DISC) ? "disconnect" : "timeout",
             s_last_reason);
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_ev, EV_DISC, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    return false;
}

// The connection task exclusively owns station transitions. Try the selected AP
// before touching the saved list, so a mistyped password preserves prior access.
static net_t s_join;
static enum { JOIN_IDLE, JOIN_QUEUED, JOIN_RUNNING, JOIN_FAILED, JOIN_SAVED } s_join_state;
bool eh_wifi_join(const char *ssid, const char *pass, bool hidden)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !pass || strlen(pass) > 64 || !s_list_mu) return false;
    lock();
    bool available = managed() && s_join_state != JOIN_RUNNING && s_join_state != JOIN_QUEUED;
    if (available) {
        strlcpy(s_join.ssid, ssid, sizeof(s_join.ssid));
        strlcpy(s_join.pass, pass, sizeof(s_join.pass));
        s_join.hidden = hidden;
        s_join_state = JOIN_QUEUED;
    }
    unlock();
    if (available) xEventGroupSetBits(s_ev, EV_RECONF);
    return available;
}

bool eh_wifi_join_saved(const char *ssid)
{
    if (!ssid || !s_list_mu) return false;
    net_t saved = {0};
    lock();
    for (int i = 0; i < s_count; i++) {
        if (!strcmp(ssid, s_nets[i].ssid)) { saved = s_nets[i]; break; }
    }
    unlock();
    bool queued = saved.ssid[0] && eh_wifi_join(saved.ssid, saved.pass, saved.hidden);
    mbedtls_platform_zeroize(&saved, sizeof(saved));
    return queued;
}

const char *eh_wifi_join_state(void)
{
    static const char *states[] = {"idle", "joining", "joining", "failed", "saved"};
    if (!managed()) return "unavailable";
    lock();
    const char *state = states[s_join_state];
    unlock();
    return state;
}

static bool join_queued(void)
{
    lock();
    bool queued = s_join_state == JOIN_QUEUED;
    unlock();
    return queued;
}

static void try_requested_network(void)
{
    lock();
    if (!managed() || s_join_state != JOIN_QUEUED) {
        unlock();
        return;
    }
    net_t requested = s_join;
    memset(s_join.pass, 0, sizeof(s_join.pass));
    s_join_state = JOIN_RUNNING;
    unlock();
    if (s_online) {
        xEventGroupClearBits(s_ev, EV_DISC);
        esp_wifi_disconnect();
        xEventGroupWaitBits(s_ev, EV_DISC, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        s_online = false;
    }
    bool ok = attempt(&requested, NULL, xTaskGetTickCount());
    if (ok && eh_wifi_add(requested.ssid, requested.pass, requested.hidden) != ESP_OK) {
        esp_wifi_disconnect();
        xEventGroupWaitBits(s_ev, EV_DISC, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        s_online = false;
        ok = false;
    }
    mbedtls_platform_zeroize(requested.pass, sizeof(requested.pass));
    lock();
    s_join_state = ok ? JOIN_SAVED : JOIN_FAILED;
    unlock();
}

static void wifi_task(void *arg)
{
    uint32_t backoff_ms = 2000;
    eh_ap_t *seen = calloc(SCAN_MAX, sizeof(eh_ap_t));
    for (;;) {
        xEventGroupWaitBits(s_ev, EV_MANAGED | EV_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
        if (join_queued()) {
            xSemaphoreTake(s_radio_mu, portMAX_DELAY);
            try_requested_network();
            xSemaphoreGive(s_radio_mu);
            continue;
        }

        if (s_online) {
            EventBits_t b = xEventGroupWaitBits(s_ev, EV_DISC | EV_RECONF, pdTRUE, pdFALSE, portMAX_DELAY);
            if (join_queued()) continue;

            if ((b & EV_RECONF) && !(b & EV_DISC)) {
                bool still_saved = false;
                lock();
                for (int i = 0; i < s_count; i++) {
                    still_saved |= strcmp(s_nets[i].ssid, s_current) == 0;
                }
                unlock();
                if (still_saved) {
                    continue; /* keep the working connection */
                }
                ESP_LOGI(EH_TAG, "wifi: current network %s was forgotten, reconnecting", s_current);
                xSemaphoreTake(s_radio_mu, portMAX_DELAY);
                if (managed()) {
                    s_online = false;
                    esp_wifi_disconnect();
                    xEventGroupWaitBits(s_ev, EV_DISC, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
                }
                xSemaphoreGive(s_radio_mu);
            }
            backoff_ms = 2000;
        }
        lock();
        int count = s_count;
        net_t nets[EH_WIFI_MAX];
        memcpy(nets, s_nets, sizeof(net_t) * count);
        unlock();
        if (count == 0) {
            ESP_LOGW(EH_TAG, "no Wi-Fi credentials (provision over USB from the bootloader)");
            xEventGroupWaitBits(s_ev, EV_RECONF, pdTRUE, pdFALSE, portMAX_DELAY);
            continue;
        }

        xSemaphoreTake(s_radio_mu, portMAX_DELAY);
        if (!managed()) {
            xSemaphoreGive(s_radio_mu);
            continue;
        }
        int nseen = seen ? scan_locked(seen, SCAN_MAX) : -1;
        /* order: visible saved networks by signal, then the rest (hidden / not seen) in saved order */
        int order[EH_WIFI_MAX], k = 0;
        bool used[EH_WIFI_MAX] = {0};
        const eh_ap_t *hints[EH_WIFI_MAX] = {0};
        for (int j = 0; j < nseen; j++) {
            for (int i = 0; i < count; i++) {
                if (!used[i] && strcmp(nets[i].ssid, seen[j].ssid) == 0) {
                    order[k++] = i;
                    used[i] = true;
                    hints[i] = &seen[j];
                }
            }
        }
        for (int i = 0; i < count; i++) {
            if (!used[i]) {
                order[k++] = i;
            }
        }
        bool ok = false;
        for (int x = 0; x < k && !ok; x++) {
            if (join_queued() || !managed()) break;
            int i = order[x];
            TickType_t started = xTaskGetTickCount();
            ok = attempt(&nets[i], hints[i], started);
            if (!ok && hints[i] && !join_queued() && attempt_remaining(started)) {
                /* Drop stale AP/channel pins without excluding other APs of this
                 * SSID. Both attempts share the original association/IP deadline. */
                ok = attempt(&nets[i], NULL, started);
            }
        }
        xSemaphoreGive(s_radio_mu);

        if (ok) {
            backoff_ms = 2000;
            /* most recently used first */
            lock();
            if (s_count > 1 && strcmp(s_nets[0].ssid, s_current) != 0) {
                for (int i = 1; i < s_count; i++) {
                    if (strcmp(s_nets[i].ssid, s_current) == 0) {
                        net_t t = s_nets[i];
                        memmove(&s_nets[1], &s_nets[0], sizeof(net_t) * i);
                        s_nets[0] = t;
                        save_locked();
                        break;
                    }
                }
            }
            unlock();
            continue;
        }
        ESP_LOGW(EH_TAG, "wifi: no saved network reachable (%d saved, %d visible), retry in %u s", count,
                 nseen < 0 ? 0 : nseen, (unsigned)(backoff_ms / 1000));
        xEventGroupWaitBits(s_ev, EV_RECONF, pdTRUE, pdFALSE, pdMS_TO_TICKS(backoff_ms));
        if (backoff_ms < 60000) {
            backoff_ms *= 2;
        }
    }
}

esp_err_t eh_wifi_init(void)
{
    s_list_mu = xSemaphoreCreateMutex();
    s_radio_mu = xSemaphoreCreateMutex();
    s_owner_mu = xSemaphoreCreateMutex();
    s_ev = xEventGroupCreate();
    if (!s_list_mu || !s_radio_mu || !s_owner_mu || !s_ev) return ESP_ERR_NO_MEM;
    load();
    return ESP_OK;
}

/* Both helpers run under the owner and radio mutexes. A failed teardown
 * retains the netif so the caller can retry without touching an app driver. */
static esp_err_t stop_station(void)
{
    if (!s_sta_netif) return ESP_OK;
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) return err;
    err = esp_wifi_deinit();
    if (err != ESP_OK) return err;
    esp_netif_destroy_default_wifi(s_sta_netif);
    s_sta_netif = NULL;
    return ESP_OK;
}

static esp_err_t start_station(void)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK || esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        return ESP_ERR_INVALID_STATE; /* the application still owns Wi-Fi */
    }
    if (!s_task_created) {
        if (xTaskCreate(wifi_task, "eh_wifi", 6144, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        s_task_created = true;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_err_t cleanup = stop_station();
        if (cleanup != ESP_OK) return cleanup;
    }
    return err;
}

esp_err_t eh_wifi_set_managed(bool enabled)
{
    if (!s_owner_mu || !s_radio_mu || !s_list_mu || !s_ev) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_owner_mu, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (enabled != managed() || (!enabled && s_sta_netif)) {
        /* Close the gate before waiting on an in-flight scan/association. New
         * requests cannot queue a join or wake the manager past this point. */
        lock();
        xEventGroupClearBits(s_ev, EV_MANAGED);
        if (s_join_state == JOIN_QUEUED) {
            mbedtls_platform_zeroize(&s_join, sizeof(s_join));
            s_join_state = JOIN_FAILED;
        }
        unlock();
        if (s_task_created) xEventGroupSetBits(s_ev, EV_CANCEL | EV_DISC | EV_RECONF);
        xSemaphoreTake(s_radio_mu, portMAX_DELAY);
        s_online = false;
        err = stop_station();
        xEventGroupClearBits(s_ev, EV_STARTED | EV_GOT_IP | EV_DISC | EV_CANCEL);
        if (enabled && err == ESP_OK) {
            err = start_station();
            if (err == ESP_OK) xEventGroupSetBits(s_ev, EV_MANAGED);
        }
        xSemaphoreGive(s_radio_mu);
    }
    xSemaphoreGive(s_owner_mu);
    return err;
}

#else /* QEMU / OpenCores Ethernet: no Wi-Fi */
esp_err_t eh_wifi_set_managed(bool enabled) { return enabled ? ESP_ERR_NOT_SUPPORTED : ESP_OK; }
bool eh_wifi_join_saved(const char *ssid) { return false; }
int eh_wifi_count(void) { return 0; }
bool eh_wifi_join(const char *ssid, const char *pass, bool hidden) { return false; }
const char *eh_wifi_join_state(void) { return "unavailable"; }
esp_err_t eh_wifi_add(const char *ssid, const char *pass, bool hidden) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t eh_wifi_forget(const char *ssid) { return ESP_ERR_NOT_SUPPORTED; }
void eh_wifi_saved_json(cJSON *arr) { (void)arr; }
int eh_wifi_scan(eh_ap_t *out, int max, uint32_t timeout_ms) { return -1; }
bool eh_wifi_current(char *ssid, size_t len) { return false; }
const char *eh_wifi_auth_str(int auth) { return "n/a"; }
#endif
