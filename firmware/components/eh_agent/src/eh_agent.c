/*
 * EspHub agent core: hub connection, framing and request dispatch.
 *
 * The agent task owns MQTT publishing, HTTPS requests and command dispatch.
 * Producer tasks only write bounded queues.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "eh_priv.h"
#include "eh_ble.h"
#include "eh_iot.h"

#define TAG EH_TAG
#define US_PER_MS 1000LL
#define BACKOFF_RESET_US (60 * 1000000LL)
#define CONNECT_TIMEOUT_US (30 * 1000000LL)
#define BACKOFF_MAX_MS 60000
#define MAX_OPS 8

eh_config_t g_eh_cfg = EH_CONFIG_DEFAULT();

typedef struct {
    char op[20];
    eh_op_handler_t fn;
} op_t;

// A binary semaphore latches work arriving between a state check and sleep.
static SemaphoreHandle_t s_wake;
void eh_agent_notify(void)
{
    if (!s_wake) return;
    if (xPortInIsrContext()) {
        BaseType_t higher = pdFALSE;
        xSemaphoreGiveFromISR(s_wake, &higher);
        if (higher) portYIELD_FROM_ISR();
    } else {
        xSemaphoreGive(s_wake);
    }
}
typedef struct { char *frame; unsigned session; } tx_item_t;
static QueueHandle_t s_txq, s_message_rx;
static atomic_uint s_message_session;
static unsigned s_message_generation;
#if CONFIG_EH_LAUNCHER
static EventGroupHandle_t ota_transport;
static atomic_bool s_result_due;

void eh_install_result_ready(void)
{
    atomic_store(&s_result_due, true);
    eh_agent_notify();
}
enum { OTA_PAUSE = BIT0, MQTT_STOPPED = BIT1 };

void eh_pause_mqtt(void)
{
    xEventGroupSetBits(ota_transport, OTA_PAUSE);
    eh_agent_notify();
    xEventGroupWaitBits(ota_transport, MQTT_STOPPED, pdFALSE, pdTRUE, portMAX_DELAY);
}

void eh_resume_mqtt(void)
{
    xEventGroupClearBits(ota_transport, OTA_PAUSE | MQTT_STOPPED);
    eh_agent_notify();
}
#endif
static volatile bool s_mqtt_up, s_hello_acked, s_mqtt_broken;
static int64_t s_conn_start_us, s_up_since_us, s_next_connect_us;
static uint32_t s_backoff_ms = 1000;
static volatile int64_t s_reboot_at_us;
static char s_boot_id[33];
static char s_install_state[20] = "queued";
static char s_inflight_id[37];
static uint64_t s_last_sequence;
static char *s_last_receipt;
static bool s_iot_started;
static bool s_started, s_cfg_ok;
static char s_hub[EH_HUB_HOST_MAX + 1], s_tok[96];
static op_t s_ops[MAX_OPS];
static int s_nops;
static uint32_t s_events_dropped;

static portMUX_TYPE s_rmux = portMUX_INITIALIZER_UNLOCKED;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void ensure_queues(void)
{
    static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
    static bool done;
    if (done) {
        return;
    }
    QueueHandle_t tx = xQueueCreate(16, sizeof(tx_item_t));
    QueueHandle_t rx = xQueueCreate(4, sizeof(char *));
    portENTER_CRITICAL(&m);
    if (!done) {
        s_txq = tx;
        s_message_rx = rx;
        done = true;
        tx = rx = NULL;
    }
    portEXIT_CRITICAL(&m);
    if (tx) vQueueDelete(tx);
    if (rx) vQueueDelete(rx);
}

/* ------------------------------------------------------------------ config */

static bool load_config(void)
{
    char hub[sizeof(s_hub)] = "", token[sizeof(s_tok)] = "";
    bool valid = eh_nvs_get_str("hub", hub, sizeof(hub)) == ESP_OK && eh_hub_host_valid(hub) &&
                 eh_nvs_get_str(EH_KEY_TOKEN, token, sizeof(token)) == ESP_OK && token[0];
    if (!valid) hub[0] = token[0] = 0;
    if (strcmp(s_hub, hub) || strcmp(s_tok, token)) {
        s_last_sequence = 0;
        s_inflight_id[0] = 0;
        free(s_last_receipt);
        s_last_receipt = NULL;
        strlcpy(s_hub, hub, sizeof(s_hub));
        strlcpy(s_tok, token, sizeof(s_tok));
    }
    return valid;
}

esp_err_t eh_hub_http_base(char *buf, size_t len)
{
    if (!s_hub[0]) return ESP_ERR_INVALID_STATE;
    int written = snprintf(buf, len, "https://%s", s_hub);
    return written >= 0 && (size_t)written < len ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void eh_http_apply_auth(esp_http_client_handle_t client)
{
    char h[sizeof(s_tok) + 8];
    snprintf(h, sizeof(h), "Bearer %s", s_tok);
    esp_http_client_set_header(client, "Authorization", h);
}

/* ------------------------------------------------------------------ public small API */

void eh_set_install_state(const char *state)
{
    strlcpy(s_install_state, state, sizeof(s_install_state));
}

bool eh_connected(void) { return s_mqtt_up && s_hello_acked; }

bool eh_mqtt_connected(void) { return s_mqtt_up; }

void eh_schedule_reboot(uint32_t delay_ms)
{
    int64_t t = now_us() + (int64_t)delay_ms * US_PER_MS;
    portENTER_CRITICAL(&s_rmux);
    s_reboot_at_us = t;
    portEXIT_CRITICAL(&s_rmux);
    eh_agent_notify();
}

esp_err_t eh_op_register(const char *op, eh_op_handler_t handler)
{
    for (int i = 0; i < s_nops; i++) {
        if (strcmp(s_ops[i].op, op) == 0) {
            s_ops[i].fn = handler;
            return ESP_OK;
        }
    }
    if (s_nops >= MAX_OPS || strlen(op) >= sizeof(s_ops[0].op)) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_ops[s_nops].op, op, sizeof(s_ops[0].op));
    s_ops[s_nops].fn = handler;
    s_nops++;
    return ESP_OK;
}

static bool txq_post(char *frame, unsigned session)
{
    ensure_queues();
    if (!frame) return false;
    tx_item_t item = {frame, session};
    if (!s_txq || xQueueSend(s_txq, &item, 0) != pdTRUE) {
        free(frame);
        if (!session) s_events_dropped++;
        return false;
    }
    eh_agent_notify();
    return true;
}

static void txq_push(char *frame)
{
    txq_post(frame, 0);
}

bool eh_message_send(const char *json, const char *reply_to)
{
    unsigned session = atomic_load(&s_message_session);
    if (!session || !json || strlen(json) > 1024 ||
        (reply_to && strlen(reply_to) != 36))
        return false;
    cJSON *data = cJSON_ParseWithOpts(json, NULL, true);
    if (!data) return false;
    cJSON *frame = cJSON_CreateObject();
    if (!frame || !cJSON_AddItemToObject(frame, "payload", data)) {
        cJSON_Delete(data);
        cJSON_Delete(frame);
        return false;
    }
    char *encoded = NULL;
    if (cJSON_AddStringToObject(frame, "t", "message") &&
        (!reply_to || cJSON_AddStringToObject(frame, "reply_to", reply_to)))
        encoded = cJSON_PrintUnformatted(frame);
    cJSON_Delete(frame);
    return txq_post(encoded, session);
}

char *eh_message_receive(void)
{
    char *message = NULL;
    if (s_message_rx) xQueueReceive(s_message_rx, &message, 0);
    return message;
}

void eh_event_json(const char *name, cJSON *data)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "event");
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddItemToObject(o, "data", data ? data : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    txq_push(s);
}

void eh_event(const char *name, const char *json)
{
    cJSON *data = json ? cJSON_Parse(json) : cJSON_CreateObject();
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "text", json);
    }
    eh_event_json(name, data);
}

/* ------------------------------------------------------------------ hello / status */

#include "eh_install.h"

/* IDF places this directly after esp_app_desc_t in the first image segment.
 * The bootloader can check the artifact type and board before erasing a slot. */
#if CONFIG_EH_LAUNCHER
#define EH_IMAGE_PROFILE EH_BL_IMAGE_PREFIX CONFIG_EH_BL_PROFILE
static const char image_profile[] __attribute__((section(".rodata_custom_desc"), aligned(4))) = EH_IMAGE_PROFILE "\0" EH_OTA_CAPABILITY;
#endif

static void report_mac(cJSON *o, const char *field, esp_mac_type_t type)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, type) != ESP_OK) return;
    char address[18];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(o, field, address);
}

static void hello_fill(cJSON *o)
{
    bool launcher = eh_is_bootloader();
    cJSON_AddStringToObject(o, "role", launcher ? "bootloader" : "app");
    char ble_address[18];
    if (eh_ble_address(ble_address)) cJSON_AddStringToObject(o, "ble_address", ble_address);
#if CONFIG_EH_NET_OPENETH
    report_mac(o, "net_mac", ESP_MAC_ETH);
#else
    report_mac(o, "net_mac", ESP_MAC_WIFI_STA);
#endif
    const esp_partition_t *bootloader = launcher ? esp_ota_get_running_partition() : eh_bootloader_partition();
    esp_app_desc_t desc;
    if (bootloader && esp_ota_get_partition_description(bootloader, &desc) == ESP_OK)
        cJSON_AddStringToObject(o, "bootloader_version", desc.version);
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *fw = cJSON_AddObjectToObject(o, "fw");
    cJSON_AddStringToObject(fw, "name", app->project_name);
    cJSON_AddStringToObject(fw, "version", app->version);
    cJSON_AddStringToObject(o, "sdk_version", EH_SDK_VERSION);
}

static void diagnostics_fill(cJSON *o)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[65];
    eh_hex(d->app_elf_sha256, 32, sha);
    const esp_partition_t *run = esp_ota_get_running_partition();
#if CONFIG_EH_LAUNCHER
    cJSON_AddStringToObject(o, "image_profile", image_profile);
    cJSON_AddStringToObject(o, "bootloader_profile", CONFIG_EH_BL_PROFILE);
#endif
    cJSON *fw = cJSON_GetObjectItem(o, "fw");
    cJSON_AddStringToObject(fw, "idf", d->idf_ver);
    cJSON_AddStringToObject(fw, "sha256", sha);
    cJSON_AddStringToObject(o, "partition", run ? run->label : "");
    cJSON_AddStringToObject(o, "ota_state", run ? eh_ota_state_str(run) : "");
    cJSON_AddStringToObject(o, "chip", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(o, "reset_reason", eh_reset_reason_str());
    char *pending = eh_nvs_get_str_alloc(EH_KEY_PENDING);
    cJSON *pj = pending ? cJSON_Parse(pending) : NULL;
    free(pending);
    if (cJSON_IsObject(pj)) {
        cJSON *pi = cJSON_AddObjectToObject(o, "pending_install");
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(pj, "kind");
        if (cJSON_IsString(kind)) cJSON_AddStringToObject(pi, "kind", kind->valuestring);
        cJSON_AddStringToObject(pi, "state", s_install_state);
    } else {
        cJSON_AddNullToObject(o, "pending_install");
    }
    cJSON_Delete(pj);
    cJSON_AddNumberToObject(o, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(o, "heap_internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(o, "heap_internal_largest",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(o, "uptime_ms", (double)(now_us() / 1000));
}

/* ------------------------------------------------------------------ socket */

static bool send_text(const char *s, size_t len)
{
    cJSON *frame = cJSON_ParseWithLength(s, len);
    if (!frame)
        return false;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "id"));
    const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "t"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "name"));
    bool final = name && (!strcmp(name, "install.done") || !strcmp(name, "install.failed"));
    bool result = t && !strcmp(t, "res");
    bool confirm = result || final || (t && !strcmp(t, "hello"));
    bool best_effort = t && !strcmp(t, "message");
    bool sent = eh_iot_publish(frame, confirm);
    if (result && id && !strcmp(id, s_inflight_id)) {
        if (sent) {
            free(s_last_receipt);
            s_last_receipt = NULL;
        } else if (s != s_last_receipt) {
            free(s_last_receipt);
            s_last_receipt = strndup(s, len);
        }
    }
    cJSON_Delete(frame);
    if (!sent && !best_effort)
        s_mqtt_broken = true;
    return sent;
}

static void mqtt_teardown(const char *why)
{
    atomic_store(&s_message_session, 0);
    char *message;
    while ((message = eh_message_receive())) free(message);
    // Reset lazily on disconnect; a healthy idle session needs no periodic wake.
    if (s_mqtt_up && now_us() - s_up_since_us >= BACKOFF_RESET_US) {
        s_backoff_ms = 1000;
    }
    if (s_iot_started) {
        eh_iot_stop();
        s_iot_started = false;
    }
    if (s_mqtt_up) {
        ESP_LOGW(TAG, "hub connection closed (%s)", why);
    }
#if CONFIG_MBEDTLS_HAVE_TIME_DATE
    /* With certificate date checks on, TLS to the hub fails until the clock is set. The SDK does
     * not start SNTP itself (apps usually own it); say so instead of leaving a bare TLS error. */
    static bool s_time_hint;
    if (!s_mqtt_up && !s_time_hint && time(NULL) < 1704067200 /* 2024-01-01 */) {
        s_time_hint = true;
        ESP_LOGW(TAG, "system time is not set and CONFIG_MBEDTLS_HAVE_TIME_DATE is on: the hub "
                      "certificate cannot be verified until the app starts SNTP");
    }
#endif
    s_mqtt_up = false;
    s_hello_acked = false;
    uint32_t delay = s_backoff_ms * (80 + esp_random() % 41) / 100; /* +-20 % jitter */
    s_next_connect_us = now_us() + (int64_t)delay * US_PER_MS;
    s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
    ESP_LOGI(TAG, "reconnect in %" PRIu32 " ms", delay);
}

static bool send_hello(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "hello");
    hello_fill(o);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (s) {
        bool ok = send_text(s, strlen(s));
        free(s);
        return ok;
    }
    return false;
}

static void drain_txq(void)
{
    tx_item_t item;
    while (xQueueReceive(s_txq, &item, 0) == pdTRUE) {
        bool ok = true;
        if (!item.session || item.session == atomic_load(&s_message_session))
            ok = send_text(item.frame, strlen(item.frame));
        free(item.frame);
        if (!ok && !item.session) break;
    }
}

/* ------------------------------------------------------------------ requests */

static void send_res(const cJSON *id, bool ok, cJSON *data, const char *err)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "res");
    cJSON_AddItemToObject(o, "id", id ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
    cJSON_AddBoolToObject(o, "ok", ok);
    if (ok) {
        cJSON_AddItemToObject(o, "data", data ? data : cJSON_CreateObject());
    } else {
        cJSON_Delete(data);
        cJSON_AddStringToObject(o, "error", err && *err ? err : "error");
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (s && strlen(s) > 16384) {
        free(s);
        send_res(id, false, NULL, "response exceeds 16 KiB");
        return;
    }
    if (s) {
        send_text(s, strlen(s));
        free(s);
    }
}

static esp_err_t run_cmd(const char *name, const cJSON *argv, cJSON *data, char *err, size_t errlen)
{
    char *out = NULL;
    int rc = eh_cmd_run(name, argv, &out);
    if (rc == -1000) {
        snprintf(err, errlen, "unknown command: %s", name);
        return ESP_ERR_NOT_FOUND;
    }
    if (rc != 0) {
        if (out && *out) {
            strlcpy(err, out, errlen);
        } else {
            snprintf(err, errlen, "exit %d", rc);
        }
        free(out);
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(data, "output", out ? out : "");
    free(out);
    return ESP_OK;
}

esp_err_t eh_select_app(void)
{
    const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "app");
    return app ? esp_ota_set_boot_partition(app) : ESP_ERR_NOT_FOUND;
}

static esp_err_t select_bootloader(void)
{
    esp_err_t e = eh_nvs_set_u8(EH_KEY_HOLD, 1);
    if (e != ESP_OK || eh_is_bootloader()) return e;
    const esp_partition_t *lp = eh_bootloader_partition();
    e = lp ? esp_ota_set_boot_partition(lp) : ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) eh_nvs_erase(EH_KEY_HOLD);
    return e;
}

static esp_err_t op_reboot(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    const cJSON *jt = cJSON_GetObjectItemCaseSensitive(args, "target");
    const char *target = cJSON_IsString(jt) ? jt->valuestring : "self";
    bool bootloader = eh_is_bootloader();
    if (strcmp(target, "app") == 0) {
        if (bootloader) {
            esp_err_t e = eh_select_app();
            if (e != ESP_OK) {
                snprintf(err, errlen, "no bootable app image: %s", esp_err_to_name(e));
                return e;
            }
        }
    } else if (strcmp(target, "bootloader") == 0) {
        esp_err_t e = select_bootloader();
        if (e != ESP_OK) {
            snprintf(err, errlen, "cannot enter bootloader: %s", esp_err_to_name(e));
            return e;
        }
    } else if (strcmp(target, "self") != 0) {
        snprintf(err, errlen, "bad target: %s", target);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "reboot requested (target=%s)", target);
    eh_schedule_reboot(300);
    return ESP_OK;
}

static void handle_req(const cJSON *root)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *jop = cJSON_GetObjectItemCaseSensitive(root, "op");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(root, "args");
    const char *op = cJSON_IsString(jop) ? jop->valuestring : "";
    cJSON *data = cJSON_CreateObject();
    char err[192] = "";
    esp_err_t r = ESP_ERR_NOT_SUPPORTED;
    for (int i = 0; i < s_nops; i++) {
        if (strcmp(s_ops[i].op, op) == 0) {
            r = s_ops[i].fn(args, data, err, sizeof(err));
            if (r == ESP_OK && !strcmp(op, "install"))
                cJSON_Delete(data);
            else
                send_res(id, r == ESP_OK, data, err);
            return;
        }
    }
    if (strcmp(op, "status") == 0) {
        hello_fill(data);
        cJSON_AddStringToObject(data, "boot_id", s_boot_id);
        diagnostics_fill(data);
        eh_net_info(data);
        cJSON_AddItemToObject(data, "partitions", eh_partitions_json());
        r = ESP_OK;
    } else if (strcmp(op, "logs.set") == 0) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(args, "enabled");
        if (!cJSON_IsBool(enabled)) {
            snprintf(err, sizeof(err), "expected boolean enabled");
            r = ESP_ERR_INVALID_ARG;
        } else {
            r = eh_console_set(cJSON_IsTrue(enabled));
            if (r == ESP_OK) cJSON_AddBoolToObject(data, "enabled", cJSON_IsTrue(enabled));
            else snprintf(err, sizeof(err), "cannot allocate console buffer");
        }
    } else if (strcmp(op, "cmd") == 0) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
        const cJSON *argv = cJSON_GetObjectItemCaseSensitive(args, "argv");
        if (!cJSON_IsString(n)) {
            snprintf(err, sizeof(err), "missing name");
            r = ESP_ERR_INVALID_ARG;
        } else {
            r = run_cmd(n->valuestring, argv, data, err, sizeof(err));
        }
    } else if (strcmp(op, "provision.reset") == 0) {
        const char *keys[] = {"mqtt_identity", "mqtt_grant", "ble_key", "hub", EH_KEY_TOKEN};
        r = eh_nvs_set_str(EH_KEY_WIFIS, "[]");
        for (size_t i = 0; r == ESP_OK && i < sizeof(keys)/sizeof(keys[0]); ++i) {
            r = eh_nvs_erase(keys[i]);
        }
        if (r == ESP_OK) r = select_bootloader();
        if (r == ESP_OK) eh_schedule_reboot(500);
        if (r != ESP_OK) snprintf(err, sizeof(err), "reset failed: %s", esp_err_to_name(r));
    } else if (strcmp(op, "reboot") == 0) {
        r = op_reboot(args, data, err, sizeof(err));
    } else if (strcmp(op, "install") == 0 && !eh_is_bootloader()) {
        snprintf(err, sizeof(err), "Enter bootloader mode before OTA");
    } else {
        snprintf(err, sizeof(err), "unknown op: %s", op);
    }
    if (r == ESP_OK && !strcmp(op, "reboot"))
        cJSON_Delete(data);
    else
        send_res(id, r == ESP_OK, data, err);
}

esp_err_t eh_enter_bootloader(void)
{
    esp_err_t e = select_bootloader();
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

// Called after hello in either role. A completed OTA may have switched slots
// before the old boot had a chance to reconnect.
static bool report_install_result(void)
{
    char *saved = eh_nvs_get_str_alloc(EH_KEY_PENDING);
    if (!saved) return true;
    cJSON *frame = cJSON_Parse(saved);
    free(saved);
    bool ok = true;
    if (cJSON_IsString(cJSON_GetObjectItem(frame, "t"))) {
        const char *label = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "target"));
        const esp_partition_t *run = esp_ota_get_running_partition();
        if (label && strcmp(label, run->label)) {
#if CONFIG_EH_LAUNCHER
            // Boot selection succeeded and the installer's reboot is still pending.
            const esp_partition_t *selected = esp_ota_get_boot_partition();
            if (selected && !strcmp(selected->label, label)) {
                eh_schedule_reboot(300);
                cJSON_Delete(frame);
                return false;
            }
#endif
            cJSON_ReplaceItemInObject(frame, "name", cJSON_CreateString("install.failed"));
            cJSON_AddStringToObject(cJSON_GetObjectItem(frame, "data"), "error", "Boot selection did not take effect");
        }
        cJSON_DeleteItemFromObject(frame, "target");
        ok = eh_iot_publish(frame, true) && eh_nvs_erase(EH_KEY_PENDING) == ESP_OK;
        if (!ok) s_mqtt_broken = true;
    }
    cJSON_Delete(frame);
    return ok;
}

static void do_reboot(void)
{
    ESP_LOGW(TAG, "rebooting");
    if (s_mqtt_up && !s_mqtt_broken) {
        drain_txq();
    }
    esp_restart();
}

static void service_control(void)
{
    cJSON *frame = eh_iot_control();
    if (!frame) return;
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(frame, "message");
    if (cJSON_IsObject(message) && s_message_rx) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(message, "id"));
        if (id && strlen(id) == 36 && cJSON_GetObjectItemCaseSensitive(message, "payload")) {
            char *encoded = cJSON_PrintUnformatted(message);
            if (encoded && xQueueSend(s_message_rx, &encoded, 0) != pdTRUE) free(encoded);
        }
        cJSON_Delete(frame);
        eh_agent_notify();
        return;
    }
    const cJSON *command = cJSON_GetObjectItem(frame, "command");
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(command, "id"));
    const cJSON *sequence = cJSON_GetObjectItem(command, "seq");
    if (id && strlen(id) == 36 && cJSON_IsNumber(sequence) &&
        sequence->valuedouble >= 1 && sequence->valuedouble <= 9007199254740991.0) {
        uint64_t seq = (uint64_t)sequence->valuedouble;
        if (seq > s_last_sequence) {
            s_last_sequence = seq;
            strlcpy(s_inflight_id, id, sizeof(s_inflight_id));
            free(s_last_receipt);
            s_last_receipt = NULL;
            handle_req(command);
        } else if (seq == s_last_sequence && !strcmp(id, s_inflight_id) && s_last_receipt) {
            send_text(s_last_receipt, strlen(s_last_receipt));
        }
    }
    cJSON_Delete(frame);
    eh_agent_notify();
}


static void agent_wait(bool transport_paused)
{
    int64_t deadline = INT64_MAX;
    if (!transport_paused) {
        if (!s_iot_started && s_cfg_ok && eh_net_has_ip()) {
            deadline = s_next_connect_us;
        } else if (s_iot_started && !s_mqtt_up) {
            deadline = s_conn_start_us + CONNECT_TIMEOUT_US;
        } else if (s_hello_acked && !s_mqtt_broken) {
            deadline = eh_console_deadline();
        }
    }
    portENTER_CRITICAL(&s_rmux);
    int64_t rb = s_reboot_at_us;
    portEXIT_CRITICAL(&s_rmux);
    int64_t now = now_us();
    if (rb && now >= rb) do_reboot();
    // A failed publish needs teardown on the next iteration, not another event.
    if (!transport_paused && s_mqtt_broken && s_iot_started) return;
    if (rb && rb < deadline) deadline = rb;
    if (deadline <= now) return;
    TickType_t wait = portMAX_DELAY;
    if (deadline != INT64_MAX) {
        // Round up so a sub-tick deadline does not spin, and reserve portMAX_DELAY
        // for waits with no deadline (even for the largest scheduled reboot).
        uint64_t ticks = (uint64_t)(deadline - now) * configTICK_RATE_HZ / 1000000 + 1;
        wait = ticks < portMAX_DELAY ? (TickType_t)ticks : portMAX_DELAY - 1;
    }
    xSemaphoreTake(s_wake, wait);
}

static void agent_task(void *arg)
{
    for (;;) {
#if CONFIG_EH_LAUNCHER
        if (xEventGroupGetBits(ota_transport) & OTA_PAUSE) {
            if (s_iot_started) mqtt_teardown("OTA download");
            xEventGroupSetBits(ota_transport, MQTT_STOPPED);
            agent_wait(true);
            s_next_connect_us = 0;
            continue;
        }
#endif
        if (s_iot_started && !eh_net_has_ip()) {
            mqtt_teardown("network down");
        }
        if (s_mqtt_broken && s_iot_started) {
            mqtt_teardown("send failed");
        }
        if (s_iot_started) {
            unsigned events = eh_iot_events();
            if (events & IOT_OFFLINE)
                mqtt_teardown("MQTT disconnected");
            else if (events & IOT_SUBSCRIBED) {
                s_up_since_us = now_us();
                s_mqtt_up = true;
                if (send_hello()) {
                    s_hello_acked = report_install_result();
                    if (s_hello_acked) {
                        if (++s_message_generation == 0) ++s_message_generation;
                        atomic_store(&s_message_session, s_message_generation);
                    }
                } else {
                    s_mqtt_broken = true;
                }
            }
        }
        if (!s_iot_started) {
            if (s_cfg_ok && eh_net_has_ip() && now_us() >= s_next_connect_us) {
                s_mqtt_broken = false;
                s_conn_start_us = now_us();
                s_iot_started = eh_iot_start(s_boot_id, s_hub, s_tok);
                if (!s_iot_started)
                    mqtt_teardown("MQTT setup failed");
            }
        } else if (!s_mqtt_up) {
            if (now_us() - s_conn_start_us >= CONNECT_TIMEOUT_US) {
                mqtt_teardown("connect timeout");
            }
        } else {
            drain_txq();
#if CONFIG_EH_LAUNCHER
            if (s_hello_acked && atomic_exchange(&s_result_due, false) && !report_install_result()) {
                s_hello_acked = false;
                atomic_store(&s_message_session, 0);
            }
#endif
            if (s_hello_acked && !s_mqtt_broken) {
                service_control();
                if (!s_mqtt_broken) eh_console_flush();
            }
            if (!eh_net_has_ip()) {
                mqtt_teardown("network down");
            }
        }
        agent_wait(false);
    }
}

#if CONFIG_EH_LAUNCHER
#include "esp_netif_sntp.h"
static void sntp_task(void *arg)
{
    /* Validate TLS certificate dates after a cold boot. */
    while (!eh_net_has_ip()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t e = esp_netif_sntp_init(&c);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(e));
    }
    vTaskDelete(NULL);
}

#endif

esp_err_t eh_agent_start(void)
{
    return eh_agent_start_with_config(NULL);
}

esp_err_t eh_agent_start_with_config(const eh_config_t *cfg)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;
    if (cfg) {
        g_eh_cfg = *cfg;
    }
#if CONFIG_EH_LAUNCHER
    ota_transport = xEventGroupCreate();
    if (!ota_transport) return ESP_ERR_NO_MEM;
#endif
    s_wake = xSemaphoreCreateBinary();
    if (!s_wake) return ESP_ERR_NO_MEM;
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#endif
    };
    esp_err_t pm_err = esp_pm_configure(&pm);
    if (pm_err != ESP_OK) ESP_LOGW(TAG, "Power management unavailable: %s", esp_err_to_name(pm_err));
#endif
    esp_err_t e = eh_nvs_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs init: %s", esp_err_to_name(e));
        return e;
    }
    ensure_queues();
    for (int i = 0; i < 4; ++i) {
        snprintf(s_boot_id + 8*i, 9, "%08" PRIx32, esp_random());
    }
    eh_registry_builtin_cmds();
    s_cfg_ok = load_config();
    e = eh_net_start(g_eh_cfg.manage_network);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "network start: %s", esp_err_to_name(e));
    }
#if CONFIG_EH_BLE_WIFI
    if (g_eh_cfg.manage_network && e == ESP_OK) eh_ble_init(); /* Launcher-only */
#endif
    const esp_app_desc_t *d = esp_app_get_description();
    ESP_LOGI(TAG, "agent start: %s %s role=%s boot_id=%s", d->project_name, d->version,
             eh_is_bootloader() ? "bootloader" : "app", s_boot_id);
    if (xTaskCreate(agent_task, "eh_agent", CONFIG_EH_TASK_STACK, NULL, CONFIG_EH_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_EH_LAUNCHER
    xTaskCreate(sntp_task, "eh_sntp", 3072, NULL, 3, NULL);
#endif
    return ESP_OK;
}
