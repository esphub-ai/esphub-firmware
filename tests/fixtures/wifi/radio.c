/* Deterministic host radio for the actual okou_wifi.c. No threads or sleeps. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "okou_priv.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#define STARTED BIT0
#define GOT_IP BIT1
#define DISC BIT2
#define RECONF BIT3
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

struct fake_mutex { bool held; };
struct fake_event_group { EventBits_t bits; };
static struct fake_mutex mutexes[2];
static unsigned mutex_count;
static struct fake_event_group events;
static TaskFunction_t task;
static jmp_buf task_end;
static bool finished_online;
static TickType_t now;
static EventBits_t pending;
static TickType_t pending_delay;
static unsigned cleanup_waits;
static TickType_t cleanup_delay;
static unsigned backoff_retries;

static wifi_ap_record_t scanned[24], air[24];
static unsigned scanned_count, air_count, scan_calls;
static unsigned scan_error_on_call;

enum outcome { AUTO, DISCONNECT, TIMEOUT, CONNECT_ERROR, CONFIG_ERROR };
static enum outcome outcomes[20];
static TickType_t response_delay[20], config_delay[20], connect_delay[20];
static int queue_join_on_attempt = -1;
static struct {
    wifi_config_t config;
    bool configured;
    bool connected;
    bool got_ip;
    uint8_t actual_bssid[6];
    uint8_t actual_channel;
    TickType_t wait_ticks;
} attempts[20];
static unsigned attempt_count, connect_calls, disconnect_calls;
static int active_attempt = -1;
static char stored[4096];
static unsigned writes, zeroizations;
static bool fail_next_save;

size_t strlcpy(char *dest, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t copied = len < size - 1 ? len : size - 1;
        memcpy(dest, src, copied);
        dest[copied] = 0;
    }
    return len;
}

void mbedtls_platform_zeroize(void *buffer, size_t len)
{
    memset(buffer, 0, len);
    zeroizations++;
}

char *okou_nvs_get_str_alloc(const char *key)
{
    assert(!strcmp(key, OKOU_KEY_WIFIS));
    return strdup(stored);
}

esp_err_t okou_nvs_set_str(const char *key, const char *value)
{
    assert(!strcmp(key, OKOU_KEY_WIFIS));
    writes++;
    if (fail_next_save) {
        fail_next_save = false;
        return ESP_FAIL;
    }
    assert(strlen(value) < sizeof(stored));
    strlcpy(stored, value, sizeof(stored));
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(mutex_count < ARRAY_LEN(mutexes));
    return &mutexes[mutex_count++];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks)
{
    if (mutex->held) {
        assert(ticks != portMAX_DELAY); /* would deadlock, not a simulated wait */
        return pdFALSE;
    }
    mutex->held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex->held);
    mutex->held = false;
    return pdTRUE;
}
EventGroupHandle_t xEventGroupCreate(void) { return &events; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{
    group->bits |= bits;
    return group->bits;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{
    EventBits_t before = group->bits;
    group->bits &= ~bits;
    return before;
}

TickType_t xTaskGetTickCount(void) { return now; }
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                      void *arg, unsigned priority, void *handle)
{
    assert(!task);
    task = fn; /* Capture the real production loop; run it deterministically. */
    return pdPASS;
}

static void deliver_pending(void)
{
    EventBits_t event = pending;
    pending = 0;
    if (event == GOT_IP) okou_wifi_on_got_ip();
    else if (event == DISC) okou_wifi_on_disconnected(201);
    else assert(false);
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
                              BaseType_t clear, BaseType_t all, TickType_t ticks)
{
    assert(!all);
    if (bits == (GOT_IP | DISC)) {
        assert(active_attempt >= 0 && attempts[active_attempt].connected);
        attempts[active_attempt].wait_ticks = ticks;
    } else if (bits == DISC) {
        /* Original disconnect cleanup has an independent, unchanged timeout. */
        assert(ticks == pdMS_TO_TICKS(1000) || ticks == pdMS_TO_TICKS(2000));
        cleanup_waits++;
    }
    if (!(group->bits & bits) && (pending & bits)) {
        if (pending_delay <= ticks) {
            now += pending_delay;
            pending_delay = 0;
            deliver_pending();
        } else {
            pending_delay -= ticks;
            now += ticks;
            return group->bits;
        }
    }
    if (group->bits & bits) {
        EventBits_t result = group->bits;
        if (clear) group->bits &= ~bits;
        return result;
    }
    if (bits == (GOT_IP | DISC) || bits == DISC) {
        assert(ticks != portMAX_DELAY);
        now += ticks; /* Expire a fake wait immediately: never wall-clock sleep. */
        return group->bits;
    }
    if (bits == RECONF && backoff_retries) {
        assert(ticks != portMAX_DELAY);
        backoff_retries--;
        now += ticks;
        return group->bits;
    }
    assert(bits == RECONF || bits == (DISC | RECONF));
    assert(!mutexes[0].held && !mutexes[1].held);
    char ssid[33];
    finished_online = okou_wifi_current(ssid, sizeof(ssid));
    longjmp(task_end, 1); /* At the production loop's quiescent boundary only. */
}

static void radio_owned(void) { assert(mutexes[1].held); }

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    radio_owned();
    assert(block && !config->show_hidden && config->scan_type == WIFI_SCAN_TYPE_ACTIVE);
    assert(config->scan_time.active.min == 60 && config->scan_time.active.max == 120);
    scan_calls++;
    return scan_calls == scan_error_on_call ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records)
{
    radio_owned();
    unsigned count = *number < scanned_count ? *number : scanned_count;
    memcpy(records, scanned, count * sizeof(*records));
    *number = count;
    return ESP_OK;
}

esp_err_t esp_wifi_clear_ap_list(void) { radio_owned(); return ESP_OK; }

esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config)
{
    radio_owned();
    assert(interface == WIFI_IF_STA && attempt_count < ARRAY_LEN(attempts));
    unsigned index = attempt_count++;
    attempts[index].config = *config;
    now += config_delay[index];
    if (outcomes[index] == CONFIG_ERROR) return ESP_FAIL;
    attempts[index].configured = true;
    active_attempt = index;
    return ESP_OK;
}

static bool same_ssid(const uint8_t configured[32], const char *ssid)
{
    char terminated[33] = {0};
    memcpy(terminated, configured, 32);
    return !strcmp(terminated, ssid);
}

esp_err_t esp_wifi_connect(void)
{
    radio_owned();
    assert(active_attempt == (int)attempt_count - 1);
    unsigned index = active_attempt;
    assert(attempts[index].configured);
    attempts[index].connected = true;
    connect_calls++;
    now += connect_delay[index];
    if ((int)index == queue_join_on_attempt) {
        assert(okou_wifi_join("requested", "manual-password", false));
    }
    if (outcomes[index] == CONNECT_ERROR) return ESP_FAIL;
    if (outcomes[index] == TIMEOUT) {
        pending = 0;
        return ESP_OK;
    }
    wifi_config_t *config = &attempts[index].config;
    int chosen = -1;
    for (unsigned i = 0; i < air_count; i++) {
        if (!same_ssid(config->sta.ssid, (const char *)air[i].ssid)) continue;
        if (config->sta.bssid_set && memcmp(config->sta.bssid, air[i].bssid, 6)) continue;
        /* Model one stale targeted-channel failure. Real ESP-IDF may search
         * other channels itself; this fake proves manager fallback, not RF. */
        if (config->sta.channel && config->sta.channel != air[i].primary) continue;
        if (chosen < 0 || air[i].rssi > air[chosen].rssi) chosen = i;
    }
    attempts[index].got_ip = chosen >= 0 && outcomes[index] != DISCONNECT;
    if (attempts[index].got_ip) {
        memcpy(attempts[index].actual_bssid, air[chosen].bssid, 6);
        attempts[index].actual_channel = air[chosen].primary;
    }
    pending = attempts[index].got_ip ? GOT_IP : DISC;
    pending_delay = response_delay[index];
    return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void)
{
    radio_owned();
    disconnect_calls++;
    pending = DISC; /* Cancels any pending association/IP from the fake radio. */
    pending_delay = cleanup_delay;
    return ESP_OK;
}

static wifi_ap_record_t ap(const char *ssid, int rssi, unsigned channel, unsigned id,
                           wifi_auth_mode_t auth)
{
    wifi_ap_record_t record = {.rssi = rssi, .primary = channel, .authmode = auth};
    strlcpy((char *)record.ssid, ssid, sizeof(record.ssid));
    record.bssid[0] = 0x02;
    record.bssid[5] = id;
    return record;
}

static void init(const char *saved)
{
    strlcpy(stored, saved, sizeof(stored));
    assert(okou_wifi_init() == ESP_OK);
    assert(task && mutex_count == 2);
    okou_wifi_on_started();
}

static void run(void)
{
    if (!setjmp(task_end)) {
        task(NULL);
        assert(false);
    }
}

static void assert_config(unsigned index, const char *ssid, bool pinned,
                          unsigned channel, unsigned id)
{
    assert(index < attempt_count);
    wifi_config_t *config = &attempts[index].config;
    assert(same_ssid(config->sta.ssid, ssid));
    assert(config->sta.scan_method == (pinned ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN));
    assert(config->sta.bssid_set == pinned);
    assert(config->sta.channel == channel);
    uint8_t bssid[6] = {0};
    if (pinned) { bssid[0] = 0x02; bssid[5] = id; }
    assert(!memcmp(config->sta.bssid, bssid, sizeof(bssid)));
    assert(config->sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL);
    assert(config->sta.pmf_cfg.capable && config->sta.sae_pwe_h2e == WPA3_SAE_PWE_BOTH);
    assert(config->sta.threshold.authmode ==
           (config->sta.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN));
}

static void assert_current(const char *expected)
{
    char current[33];
    assert(finished_online);
    assert(okou_wifi_current(current, sizeof(current)));
    assert(!strcmp(current, expected));
}

static void assert_saved(unsigned index, const char *ssid, bool hidden)
{
    cJSON *saved = cJSON_Parse(stored);
    assert(saved);
    const cJSON *entry = cJSON_GetArrayItem(saved, index);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(entry, "s")->valuestring, ssid));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "h")) == hidden);
    cJSON_Delete(saved);
}

static void mesh_setup(void)
{
    scanned[0] = ap("mesh", -25, 1, 1, WIFI_AUTH_WPA2_PSK);
    scanned[1] = ap("mesh", -60, 6, 2, WIFI_AUTH_WPA2_PSK);
    scanned_count = 2;
    air[0] = scanned[0];
    air_count = 1;
    init("[{\"s\":\"mesh\",\"p\":\"mesh-password\"}]");
}

static void scan_test(void)
{
    scanned[0] = ap("mesh", -80, 1, 1, WIFI_AUTH_WPA_PSK);
    scanned[1] = ap("other", -55, 3, 3, WIFI_AUTH_OPEN);
    scanned[2] = ap("", -5, 8, 4, WIFI_AUTH_OPEN);
    scanned[3] = ap("mesh", -20, 11, 2, WIFI_AUTH_WPA3_PSK);
    scanned[4] = ap("weak", -90, 7, 5, WIFI_AUTH_WEP);
    scanned_count = 5;
    init("[]");
    okou_ap_t out[24] = {0};
    assert(xSemaphoreTake(&mutexes[1], 0) == pdTRUE);
    assert(okou_wifi_scan(out, 24, 0) == -1 && scan_calls == 0);
    xSemaphoreGive(&mutexes[1]);
    assert(okou_wifi_scan(out, 24, 0) == 3);
    assert(!strcmp(out[0].ssid, "mesh") && out[0].rssi == -20 && out[0].channel == 11);
    assert(out[0].auth == WIFI_AUTH_WPA3_PSK && !memcmp(out[0].bssid, scanned[3].bssid, 6));
    assert(!strcmp(out[1].ssid, "other") && !strcmp(out[2].ssid, "weak"));
    assert(writes == 0 && attempt_count == 0);
}

static void order_test(bool succeed_first)
{
    scanned[0] = ap("weak", -65, 3, 3, WIFI_AUTH_WPA2_PSK);
    scanned[1] = ap("strong", -75, 1, 1, WIFI_AUTH_WPA_PSK);
    scanned[2] = ap("unsaved", -5, 6, 4, WIFI_AUTH_OPEN);
    scanned[3] = ap("strong", -20, 11, 2, WIFI_AUTH_WPA3_PSK);
    scanned_count = 4;
    memcpy(air, scanned, sizeof(scanned));
    air_count = scanned_count;
    init("[{\"s\":\"weak\",\"p\":\"weak-password\"},"
         "{\"s\":\"hidden\",\"p\":\"hidden-password\",\"h\":true},"
         "{\"s\":\"strong\",\"p\":\"strong-password\"}]");
    if (!succeed_first) outcomes[0] = outcomes[1] = DISCONNECT;
    run();
    assert_config(0, "strong", true, 11, 2);
    assert(!memcmp(attempts[0].config.sta.password, "strong-password", 15));
    assert(scan_calls == 1);
    if (succeed_first) {
        assert(attempt_count == 1 && connect_calls == 1 && disconnect_calls == 0);
        assert_current("strong");
        assert(writes == 1); /* Existing success/MRU policy, no scan-hint persistence. */
        assert_saved(0, "strong", false);
    } else {
        assert(attempt_count == 3);
        assert_config(1, "strong", false, 0, 0);
        assert_config(2, "weak", true, 3, 3);
        assert(attempts[0].wait_ticks == 15000 && attempts[1].wait_ticks == 14993);
        assert(attempts[2].wait_ticks == 15000); /* Each SSID gets its own budget. */
        assert_current("weak");
        assert(writes == 0);
    }
}

static void recovery_test(const char *scenario)
{
    mesh_setup();
    if (!strcmp(scenario, "other-ap")) air[0] = scanned[1];
    if (!strcmp(scenario, "channel-moved")) air[0].primary = 11;
    if (!strcmp(scenario, "connect-error")) outcomes[0] = CONNECT_ERROR;
    if (!strcmp(scenario, "config-error")) outcomes[0] = CONFIG_ERROR;
    run();
    assert_current("mesh");
    assert(attempt_count == 2 && scan_calls == 1);
    assert_config(0, "mesh", true, 1, 1);
    assert_config(1, "mesh", false, 0, 0);
    assert(attempts[1].got_ip);
    assert(attempts[1].actual_channel == air[0].primary);
    assert(!memcmp(attempts[1].actual_bssid, air[0].bssid, 6));
    assert(connect_calls == (!strcmp(scenario, "config-error") ? 1U : 2U));
    assert(writes == 0);
}

static void hidden_test(bool hidden)
{
    scanned[0] = ap("visible", -20, 1, 1, WIFI_AUTH_WPA2_PSK);
    scanned_count = 1;
    air[0] = ap("fallback", -80, 9, 9, WIFI_AUTH_OPEN);
    air_count = 1;
    init(hidden ? "[{\"s\":\"fallback\",\"p\":\"\",\"h\":true},{\"s\":\"visible\",\"p\":\"visible-password\"}]"
                : "[{\"s\":\"fallback\",\"p\":\"\"},{\"s\":\"visible\",\"p\":\"visible-password\"}]");
    run();
    assert_current("fallback");
    assert(attempt_count == 3);
    assert_config(0, "visible", true, 1, 1);
    assert_config(1, "visible", false, 0, 0);
    assert_config(2, "fallback", false, 0, 0);
    assert_saved(0, "fallback", hidden);
    assert(writes == 0);
}

static void scan_error_test(bool after_first_cycle)
{
    if (after_first_cycle) {
        mesh_setup();
    } else {
        scanned[0] = air[0] = ap("mesh", -25, 1, 1, WIFI_AUTH_WPA2_PSK);
        scanned_count = air_count = 1;
        init("[{\"s\":\"absent\",\"p\":\"\"},{\"s\":\"mesh\",\"p\":\"mesh-password\"}]");
    }
    scan_error_on_call = after_first_cycle ? 2 : 1;
    if (after_first_cycle) {
        outcomes[0] = outcomes[1] = DISCONNECT;
        backoff_retries = 1;
    }
    run();
    assert_current("mesh");
    if (after_first_cycle) {
        assert(attempt_count == 3 && scan_calls == 2);
        assert_config(0, "mesh", true, 1, 1);
        assert_config(1, "mesh", false, 0, 0);
        assert_config(2, "mesh", false, 0, 0);
    } else {
        assert(attempt_count == 2 && scan_calls == 1);
        assert_config(0, "absent", false, 0, 0);
        assert_config(1, "mesh", false, 0, 0);
    }
}

static void manual_test(const char *scenario)
{
    bool hidden = !strcmp(scenario, "manual-hidden");
    bool failed = !strcmp(scenario, "manual-failed");
    bool save_failed = !strcmp(scenario, "save-failed");
    scanned[0] = ap("requested", -20, 1, 1, WIFI_AUTH_WPA2_PSK);
    scanned[1] = ap("saved", -65, 6, 2, WIFI_AUTH_WPA2_PSK);
    scanned_count = 2;
    air[0] = ap("requested", -60, 11, 3, WIFI_AUTH_WPA2_PSK);
    air[1] = scanned[1];
    air_count = 2;
    init("[{\"s\":\"saved\",\"p\":\"saved-password\"}]");
    okou_ap_t out[24];
    assert(okou_wifi_scan(out, 24, 0) == 2); /* Deliberately stale public scan. */
    assert(okou_wifi_join("requested", hidden ? "" : "manual-password", hidden));
    assert(!strcmp(okou_wifi_join_state(), "joining"));
    assert(!okou_wifi_join("busy", "password", false));
    assert(writes == 0 && okou_wifi_count() == 1);
    if (failed) outcomes[0] = DISCONNECT;
    if (save_failed) fail_next_save = true;
    run();
    assert_config(0, "requested", false, 0, 0);
    assert(zeroizations == 1);
    if (failed || save_failed) {
        assert(!strcmp(okou_wifi_join_state(), "failed"));
        assert_current("saved");
        assert(okou_wifi_count() == 1 && attempt_count == 2 && scan_calls == 2);
        assert_config(1, "saved", true, 6, 2);
        assert_saved(0, "saved", false);
        assert(writes == (save_failed ? 1U : 0U));
    } else {
        assert(!strcmp(okou_wifi_join_state(), "saved"));
        assert_current("requested");
        assert(attempt_count == 1 && scan_calls == 1 && writes == 1);
        assert_saved(0, "requested", hidden);
        assert_saved(1, "saved", false);
        assert(okou_wifi_count() == 2);
    }
}

static void queued_test(void)
{
    mesh_setup();
    air[1] = ap("requested", -65, 6, 3, WIFI_AUTH_WPA2_PSK);
    air_count = 2;
    queue_join_on_attempt = 0;
    outcomes[0] = DISCONNECT;
    run();
    assert_current("requested");
    assert(attempt_count == 2);
    assert_config(0, "mesh", true, 1, 1);
    assert_config(1, "requested", false, 0, 0);
    assert(!strcmp(okou_wifi_join_state(), "saved"));
}

static void budget_test(const char *scenario)
{
    mesh_setup();
    cleanup_delay = 250;
    if (!strcmp(scenario, "timeout")) {
        outcomes[0] = TIMEOUT;
        run();
        assert(!finished_online && attempt_count == 1);
        assert(attempts[0].wait_ticks == 15000 && cleanup_waits == 1);
        assert(now == 15250); /* Only independent disconnect cleanup is extra. */
        return;
    }
    bool wrap = !strcmp(scenario, "budget-wrap");
    bool exhausted = !strcmp(scenario, "budget-fallback-timeout");
    bool last_tick = !strcmp(scenario, "budget-last-tick");
    bool disc_expired = !strcmp(scenario, "budget-disc-expired");
    if (wrap) now = UINT32_MAX - 4000;
    TickType_t origin = now;
    air[0] = scanned[1]; /* Recovery must reach the other AP of this same SSID. */
    config_delay[0] = 200;
    connect_delay[0] = 300;
    response_delay[0] = last_tick ? 14249 : disc_expired ? 14500 : 5500;
    response_delay[1] = last_tick ? 1 : exhausted ? 9000 : 8000;
    run();
    assert(attempts[0].wait_ticks == 14500); /* Config/connect time is in budget. */
    if (disc_expired) {
        assert(!finished_online && attempt_count == 1 && cleanup_waits == 1);
        assert((TickType_t)(now - origin) == 15250);
        return;
    }
    assert(attempt_count == 2);
    assert_config(1, "mesh", false, 0, 0);
    assert(attempts[1].wait_ticks == (last_tick ? 1U : 8750U));
    if (exhausted) {
        assert(!finished_online && cleanup_waits == 2);
        assert((TickType_t)(now - origin) == 15250);
    } else {
        assert_current("mesh");
        assert(cleanup_waits == 1);
        assert(attempts[1].actual_bssid[5] == 2 && attempts[1].actual_channel == 6);
        assert((TickType_t)(now - origin) == (last_tick ? 15000U : 14250U));
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    for (unsigned i = 0; i < ARRAY_LEN(response_delay); i++) response_delay[i] = 7;
    const char *scenario = argv[1];
    if (!strcmp(scenario, "scan")) scan_test();
    else if (!strcmp(scenario, "fresh")) order_test(true);
    else if (!strcmp(scenario, "order")) order_test(false);
    else if (!strcmp(scenario, "other-ap") || !strcmp(scenario, "channel-moved") ||
             !strcmp(scenario, "connect-error") || !strcmp(scenario, "config-error")) recovery_test(scenario);
    else if (!strcmp(scenario, "hidden")) hidden_test(true);
    else if (!strcmp(scenario, "unseen")) hidden_test(false);
    else if (!strcmp(scenario, "scan-error")) scan_error_test(false);
    else if (!strcmp(scenario, "rescan-error")) scan_error_test(true);
    else if (!strcmp(scenario, "manual") || !strcmp(scenario, "manual-hidden") ||
             !strcmp(scenario, "manual-failed") || !strcmp(scenario, "save-failed")) manual_test(scenario);
    else if (!strcmp(scenario, "queued")) queued_test();
    else if (!strcmp(scenario, "timeout") || !strcmp(scenario, "budget") ||
             !strcmp(scenario, "budget-wrap") || !strcmp(scenario, "budget-fallback-timeout") ||
             !strcmp(scenario, "budget-last-tick") || !strcmp(scenario, "budget-disc-expired")) budget_test(scenario);
    else assert(false);
    return 0;
}
