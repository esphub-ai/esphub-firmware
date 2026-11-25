/*
 * okou agent core: hub connection, framing and request dispatch.
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
#include "okou_priv.h"
#include "okou_ble.h"
#include "okou_iot.h"

#define TAG OKOU_TAG
#define US_PER_MS 1000LL
#define BACKOFF_RESET_US (60 * 1000000LL)
#define CONNECT_TIMEOUT_US (30 * 1000000LL)
#define BACKOFF_MAX_MS 60000
#define MAX_OPS 8

okou_config_t g_okou_cfg = OKOU_CONFIG_DEFAULT();

typedef struct {
    cJSON *id;
    char name[24];
    cJSON *argv;
} poll_job_t;

typedef struct {
    char op[20];
    okou_op_handler_t fn;
    okou_op_async_handler_t afn;
} op_t;

struct okou_req {
    cJSON *id;
};

// A binary semaphore latches work arriving between a state check and sleep.
static SemaphoreHandle_t s_wake;
void okou_agent_notify(void)
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
static QueueHandle_t s_txq, s_pollq;
#if CONFIG_OKL_URL_OTA
static EventGroupHandle_t ota_transport;
static atomic_bool s_result_due;

void okou_install_result_ready(void)
{
    atomic_store(&s_result_due, true);
    okou_agent_notify();
}
enum { OTA_PAUSE = BIT0, MQTT_STOPPED = BIT1 };

void okou_pause_mqtt(void)
{
    xEventGroupSetBits(ota_transport, OTA_PAUSE);
    okou_agent_notify();
    xEventGroupWaitBits(ota_transport, MQTT_STOPPED, pdFALSE, pdTRUE, portMAX_DELAY);
}

void okou_resume_mqtt(void)
{
    xEventGroupClearBits(ota_transport, OTA_PAUSE | MQTT_STOPPED);
    okou_agent_notify();
}
#endif
static volatile bool s_mqtt_up, s_hello_acked, s_mqtt_broken, s_reload;
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
static char s_hub[OKOU_HUB_HOST_MAX + 1], s_tok[96];
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
    QueueHandle_t tx = xQueueCreate(16, sizeof(char *));
    QueueHandle_t pq = xQueueCreate(4, sizeof(poll_job_t *));
    portENTER_CRITICAL(&m);
    if (!done) {
        s_txq = tx;
        s_pollq = pq;
        done = true;
        tx = pq = NULL;
    }
    portEXIT_CRITICAL(&m);
    if (tx) {
        vQueueDelete(tx);
        vQueueDelete(pq);
    }
}

/* ------------------------------------------------------------------ config */

static bool load_config(void)
{
    char hub[sizeof(s_hub)] = "", token[sizeof(s_tok)] = "";
    bool valid = okou_nvs_get_str("hub", hub, sizeof(hub)) == ESP_OK && okou_hub_host_valid(hub) &&
                 okou_nvs_get_str(OKOU_KEY_TOKEN, token, sizeof(token)) == ESP_OK && token[0];
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

esp_err_t okou_hub_http_base(char *buf, size_t len)
{
    if (!s_hub[0]) return ESP_ERR_INVALID_STATE;
    int written = snprintf(buf, len, "https://%s", s_hub);
    return written >= 0 && (size_t)written < len ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void okou_http_apply_auth(esp_http_client_handle_t client)
{
    char h[sizeof(s_tok) + 8];
    snprintf(h, sizeof(h), "Bearer %s", s_tok);
    esp_http_client_set_header(client, "Authorization", h);
}

/* ------------------------------------------------------------------ public small API */

void okou_set_install_state(const char *state)
{
    strlcpy(s_install_state, state, sizeof(s_install_state));
}

bool okou_connected(void) { return s_mqtt_up && s_hello_acked; }

bool okou_mqtt_connected(void) { return s_mqtt_up; }

void okou_agent_reload_config(void)
{
    s_reload = true;
    okou_agent_notify();
}

void okou_schedule_reboot(uint32_t delay_ms)
{
    int64_t t = now_us() + (int64_t)delay_ms * US_PER_MS;
    portENTER_CRITICAL(&s_rmux);
    s_reboot_at_us = t;
    portEXIT_CRITICAL(&s_rmux);
    okou_agent_notify();
}

static esp_err_t op_register(const char *op, okou_op_handler_t handler, okou_op_async_handler_t ahandler)
{
    for (int i = 0; i < s_nops; i++) {
        if (strcmp(s_ops[i].op, op) == 0) {
            s_ops[i].fn = handler;
            s_ops[i].afn = ahandler;
            return ESP_OK;
        }
    }
    if (s_nops >= MAX_OPS || strlen(op) >= sizeof(s_ops[0].op)) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_ops[s_nops].op, op, sizeof(s_ops[0].op));
    s_ops[s_nops].fn = handler;
    s_ops[s_nops].afn = ahandler;
    s_nops++;
    return ESP_OK;
}

esp_err_t okou_op_register(const char *op, okou_op_handler_t handler)
{
    return op_register(op, handler, NULL);
}

esp_err_t okou_op_register_async(const char *op, okou_op_async_handler_t handler)
{
    return op_register(op, NULL, handler);
}

static void txq_push(char *frame)
{
    ensure_queues();
    if (!frame) {
        return;
    }
    if (xQueueSend(s_txq, &frame, 0) != pdTRUE) {
        free(frame);
        s_events_dropped++;
    } else {
        okou_agent_notify();
    }
}

void okou_event_json(const char *name, cJSON *data)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "event");
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddItemToObject(o, "data", data ? data : cJSON_CreateObject());
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    txq_push(s);
}

void okou_event(const char *name, const char *json)
{
    cJSON *data = json ? cJSON_Parse(json) : cJSON_CreateObject();
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "text", json);
    }
    okou_event_json(name, data);
}

/* ------------------------------------------------------------------ hello / status */

#include "okou_install.h"

/* IDF places this directly after esp_app_desc_t in the first image segment.
 * The bootloader can check the artifact type and board before erasing a slot. */
#if CONFIG_OKL_URL_OTA
#define OKOU_IMAGE_PROFILE OKOU_BL_IMAGE_PREFIX CONFIG_OKOU_BL_PROFILE
#elif CONFIG_OKOU_MICROPYTHON_RUNTIME
#define OKOU_IMAGE_PROFILE OKOU_MICROPYTHON_IMAGE_PREFIX CONFIG_OKOU_BOARD_PROFILE
#endif
#ifdef OKOU_IMAGE_PROFILE
static const char image_profile[] __attribute__((section(".rodata_custom_desc"), aligned(4))) = OKOU_IMAGE_PROFILE "\0" OKOU_OTA_CAPABILITY;
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
    bool launcher = okou_is_bootloader();
    cJSON_AddStringToObject(o, "role", launcher ? "bootloader" : "app");
    char ble_address[18];
    if (okou_ble_address(ble_address)) cJSON_AddStringToObject(o, "ble_address", ble_address);
#if CONFIG_OKOU_NET_OPENETH
    report_mac(o, "net_mac", ESP_MAC_ETH);
#else
    report_mac(o, "net_mac", ESP_MAC_WIFI_STA);
#endif
    const esp_partition_t *bootloader = launcher ? esp_ota_get_running_partition() : okou_bootloader_partition();
    esp_app_desc_t desc;
    if (bootloader && esp_ota_get_partition_description(bootloader, &desc) == ESP_OK)
        cJSON_AddStringToObject(o, "bootloader_version", desc.version);
    if (!launcher) {
        const char *micropython = g_okou_cfg.micropython_version;
        cJSON_AddStringToObject(o, "runtime_type", micropython ? "micropython" : "native");
        cJSON_AddStringToObject(o, "runtime_version", esp_app_get_description()->version);
    }
}

static void diagnostics_fill(cJSON *o)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[65];
    okou_hex(d->app_elf_sha256, 32, sha);
    const esp_partition_t *run = esp_ota_get_running_partition();
#ifdef OKOU_IMAGE_PROFILE
    cJSON_AddStringToObject(o, "image_profile", image_profile);
#if CONFIG_OKL_URL_OTA
    cJSON_AddBoolToObject(o, "dual_bootloader_ota",
                         !strcmp(run->label, "launcher_a") || !strcmp(run->label, "launcher_b"));
#endif
    if (CONFIG_OKOU_BL_PROFILE[0]) cJSON_AddStringToObject(o, "bootloader_profile", CONFIG_OKOU_BL_PROFILE);
#endif
    cJSON *fw = cJSON_AddObjectToObject(o, "fw");
    cJSON_AddStringToObject(fw, "name", d->project_name);
    cJSON_AddStringToObject(fw, "version", d->version);
    cJSON_AddStringToObject(fw, "idf", d->idf_ver);
    cJSON_AddStringToObject(fw, "sha256", sha);
    cJSON_AddStringToObject(o, "sdk_version", OKOU_SDK_VERSION);
    cJSON_AddStringToObject(o, "partition", run ? run->label : "");
    cJSON_AddStringToObject(o, "ota_state", run ? okou_ota_state_str(run) : "");
    cJSON_AddStringToObject(o, "chip", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(o, "reset_reason", okou_reset_reason_str());
    char *pending = okou_nvs_get_str_alloc(OKOU_KEY_PENDING);
    cJSON *pj = pending ? cJSON_Parse(pending) : NULL;
    free(pending);
    const cJSON *psha = cJSON_GetObjectItemCaseSensitive(pj, "sha256");
    if (cJSON_IsObject(pj)) {
        cJSON *pi = cJSON_AddObjectToObject(o, "pending_install");
        if (cJSON_IsString(psha)) cJSON_AddStringToObject(pi, "sha256", psha->valuestring);
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
    bool sent = okou_iot_publish(frame, confirm);
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
    if (!sent)
        s_mqtt_broken = true;
    return sent;
}

static void mqtt_teardown(const char *why)
{
    // Reset lazily on disconnect; a healthy idle session needs no periodic wake.
    if (s_mqtt_up && now_us() - s_up_since_us >= BACKOFF_RESET_US) {
        s_backoff_ms = 1000;
    }
    if (s_iot_started) {
        okou_iot_stop();
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
    char *s;
    while (xQueueReceive(s_txq, &s, 0) == pdTRUE) {
        bool ok = send_text(s, strlen(s));
        free(s);
        if (!ok) {
            break; /* dropped; the socket is going down */
        }
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

static char *build_res_string(const cJSON *id, bool ok, cJSON *data, const char *err)
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
    return s;
}

void okou_op_reply(okou_req_t *req, bool ok, cJSON *data, const char *err)
{
    if (!req) {
        cJSON_Delete(data);
        return;
    }
    char *s = build_res_string(req->id, ok, data, err);
    cJSON_Delete(req->id);
    free(req);
    if (s && strlen(s) > 16384) {
        free(s);
        s = NULL;
    }
    txq_push(s); /* sent by the agent task; dropped if the hub is not connected */
}

static esp_err_t run_cmd(const char *name, const cJSON *argv, cJSON *data, char *err, size_t errlen)
{
    char *out = NULL;
    int rc = okou_cmd_run(name, argv, &out);
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

esp_err_t okou_select_app(void)
{
    const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "app");
    return app ? esp_ota_set_boot_partition(app) : ESP_ERR_NOT_FOUND;
}

static esp_err_t select_bootloader(void)
{
    esp_err_t e = okou_nvs_set_u8(OKOU_KEY_HOLD, 1);
    if (e != ESP_OK || okou_is_bootloader()) return e;
    const esp_partition_t *lp = okou_bootloader_partition();
    e = lp ? esp_ota_set_boot_partition(lp) : ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) okou_nvs_erase(OKOU_KEY_HOLD);
    return e;
}

static esp_err_t op_reboot(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    const cJSON *jt = cJSON_GetObjectItemCaseSensitive(args, "target");
    const char *target = cJSON_IsString(jt) ? jt->valuestring : "self";
    bool bootloader = okou_is_bootloader();
    if (strcmp(target, "app") == 0) {
        if (bootloader) {
            esp_err_t e = okou_select_app();
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
    okou_schedule_reboot(300);
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
            if (s_ops[i].afn) {
                okou_req_t *req = calloc(1, sizeof(*req));
                if (req) {
                    req->id = id ? cJSON_Duplicate(id, true) : cJSON_CreateNull();
                }
                r = req && req->id ? s_ops[i].afn(req, args, err, sizeof(err)) : ESP_ERR_NO_MEM;
                if (r == ESP_OK) {
                    cJSON_Delete(data); /* answered later through okou_op_reply() */
                    return;
                }
                if (req) {
                    cJSON_Delete(req->id);
                    free(req);
                }
                if (!err[0] && r == ESP_ERR_NO_MEM) {
                    snprintf(err, sizeof(err), "out of memory");
                }
                send_res(id, false, data, err);
                return;
            }
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
        okou_net_info(data);
        cJSON_AddItemToObject(data, "partitions", okou_partitions_json());
        r = ESP_OK;
    } else if (strcmp(op, "logs.set") == 0) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(args, "enabled");
        if (!cJSON_IsBool(enabled)) {
            snprintf(err, sizeof(err), "expected boolean enabled");
            r = ESP_ERR_INVALID_ARG;
        } else {
            r = okou_console_set(cJSON_IsTrue(enabled));
            if (r == ESP_OK) cJSON_AddBoolToObject(data, "enabled", cJSON_IsTrue(enabled));
            else snprintf(err, sizeof(err), "cannot allocate console buffer");
        }
    } else if (strcmp(op, "cmd") == 0) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
        const cJSON *argv = cJSON_GetObjectItemCaseSensitive(args, "argv");
        if (!cJSON_IsString(n)) {
            snprintf(err, sizeof(err), "missing name");
            r = ESP_ERR_INVALID_ARG;
        } else if (g_okou_cfg.handlers_in_poll) {
            poll_job_t *j = calloc(1, sizeof(*j));
            if (j) {
                j->id = cJSON_Duplicate(id, true);
                strlcpy(j->name, n->valuestring, sizeof(j->name));
                j->argv = argv ? cJSON_Duplicate(argv, true) : NULL;
            }
            if (j && xQueueSend(s_pollq, &j, 0) == pdTRUE) {
                cJSON_Delete(data);
                return; /* answered from okou_poll() */
            }
            if (j) {
                cJSON_Delete(j->id);
                cJSON_Delete(j->argv);
                free(j);
            }
            snprintf(err, sizeof(err), "busy: app has not polled");
            r = ESP_ERR_NO_MEM;
        } else {
            r = run_cmd(n->valuestring, argv, data, err, sizeof(err));
        }
    } else if (strcmp(op, "cmd.list") == 0) {
        cJSON_AddItemToObject(data, "commands", okou_cmd_list_json());
        r = ESP_OK;
    } else if (strcmp(op, "param.list") == 0) {
        cJSON_AddItemToObject(data, "params", okou_param_list_json());
        r = ESP_OK;
    } else if (strcmp(op, "param.get") == 0) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
        r = okou_param_get_json(cJSON_IsString(n) ? n->valuestring : NULL, data, err, sizeof(err));
    } else if (strcmp(op, "param.set") == 0) {
        r = okou_param_set_json(args, data, err, sizeof(err));
    } else if (strcmp(op, "provision.reset") == 0) {
        const char *keys[] = {"mqtt_identity", "mqtt_grant", "ble_key", "hub", OKOU_KEY_TOKEN};
        r = okou_nvs_set_str(OKOU_KEY_WIFIS, "[]");
        for (size_t i = 0; r == ESP_OK && i < sizeof(keys)/sizeof(keys[0]); ++i) {
            r = okou_nvs_erase(keys[i]);
        }
        if (r == ESP_OK) r = select_bootloader();
        if (r == ESP_OK) okou_schedule_reboot(500);
        if (r != ESP_OK) snprintf(err, sizeof(err), "reset failed: %s", esp_err_to_name(r));
    } else if (strcmp(op, "reboot") == 0) {
        r = op_reboot(args, data, err, sizeof(err));
    } else if (strcmp(op, "install") == 0 && !okou_is_bootloader()) {
        snprintf(err, sizeof(err), "Enter bootloader mode before OTA");
    } else {
        snprintf(err, sizeof(err), "unknown op: %s", op);
    }
    if (r == ESP_OK && !strcmp(op, "reboot"))
        cJSON_Delete(data);
    else
        send_res(id, r == ESP_OK, data, err);
}

int okou_poll(uint32_t timeout_ms)
{
    ensure_queues();
    int n = 0;
    poll_job_t *j;
    while (xQueueReceive(s_pollq, &j, n == 0 ? pdMS_TO_TICKS(timeout_ms) : 0) == pdTRUE) {
        cJSON *data = cJSON_CreateObject();
        char err[192] = "";
        esp_err_t r = run_cmd(j->name, j->argv, data, err, sizeof(err));
        txq_push(build_res_string(j->id, r == ESP_OK, data, err));
        cJSON_Delete(j->id);
        cJSON_Delete(j->argv);
        free(j);
        n++;
    }
    return n;
}

esp_err_t okou_enter_bootloader(void)
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
    char *saved = okou_nvs_get_str_alloc(OKOU_KEY_PENDING);
    if (!saved) return true;
    cJSON *frame = cJSON_Parse(saved);
    free(saved);
    bool ok = true;
    if (cJSON_IsString(cJSON_GetObjectItem(frame, "t"))) {
        const char *label = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "target"));
        const esp_partition_t *run = esp_ota_get_running_partition();
        if (label && strcmp(label, run->label)) {
#if CONFIG_OKL_URL_OTA
            // Boot selection succeeded and the installer's reboot is still pending.
            const esp_partition_t *selected = esp_ota_get_boot_partition();
            if (selected && !strcmp(selected->label, label)) {
                okou_schedule_reboot(300);
                cJSON_Delete(frame);
                return false;
            }
#endif
            cJSON_ReplaceItemInObject(frame, "name", cJSON_CreateString("install.failed"));
            cJSON_AddStringToObject(cJSON_GetObjectItem(frame, "data"), "error", "Boot selection did not take effect");
        }
        cJSON_DeleteItemFromObject(frame, "target");
        ok = okou_iot_publish(frame, true) && okou_nvs_erase(OKOU_KEY_PENDING) == ESP_OK;
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
    cJSON *frame = okou_iot_control();
    if (!frame) return;
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
    okou_agent_notify();
}


static void agent_wait(bool transport_paused)
{
    int64_t deadline = INT64_MAX;
    if (!transport_paused) {
        if (!s_iot_started && s_cfg_ok && okou_net_has_ip()) {
            deadline = s_next_connect_us;
        } else if (s_iot_started && !s_mqtt_up) {
            deadline = s_conn_start_us + CONNECT_TIMEOUT_US;
        } else if (s_hello_acked && !s_mqtt_broken) {
            deadline = okou_console_deadline();
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
        if (s_reload) {
            s_reload = false;
            okou_console_set(false);
            s_cfg_ok = load_config();
            if (s_iot_started) {
                mqtt_teardown("config reload");
            }
            s_backoff_ms = 1000;
            s_next_connect_us = now_us();
        }
#if CONFIG_OKL_URL_OTA
        if (xEventGroupGetBits(ota_transport) & OTA_PAUSE) {
            if (s_iot_started) mqtt_teardown("OTA download");
            xEventGroupSetBits(ota_transport, MQTT_STOPPED);
            agent_wait(true);
            s_next_connect_us = 0;
            continue;
        }
#endif
        if (s_iot_started && !okou_net_has_ip()) {
            mqtt_teardown("network down");
        }
        if (s_mqtt_broken && s_iot_started) {
            mqtt_teardown("send failed");
        }
        if (s_iot_started) {
            unsigned events = okou_iot_events();
            if (events & IOT_OFFLINE)
                mqtt_teardown("MQTT disconnected");
            else if (events & IOT_SUBSCRIBED) {
                s_up_since_us = now_us();
                s_mqtt_up = true;
                if (send_hello()) {
                    s_hello_acked = report_install_result();
                } else {
                    s_mqtt_broken = true;
                }
            }
        }
        if (!s_iot_started) {
            if (s_cfg_ok && okou_net_has_ip() && now_us() >= s_next_connect_us) {
                s_mqtt_broken = false;
                s_conn_start_us = now_us();
                s_iot_started = okou_iot_start(s_boot_id, s_hub, s_tok);
                if (!s_iot_started)
                    mqtt_teardown("MQTT setup failed");
            }
        } else if (!s_mqtt_up) {
            if (now_us() - s_conn_start_us >= CONNECT_TIMEOUT_US) {
                mqtt_teardown("connect timeout");
            }
        } else {
            drain_txq();
#if CONFIG_OKL_URL_OTA
            if (s_hello_acked && atomic_exchange(&s_result_due, false) && !report_install_result())
                s_hello_acked = false;
#endif
            if (s_hello_acked && !s_mqtt_broken) {
                service_control();
                if (!s_mqtt_broken) okou_console_flush();
            }
            if (!okou_net_has_ip()) {
                mqtt_teardown("network down");
            }
        }
        agent_wait(false);
    }
}

#if CONFIG_OKL_URL_OTA || CONFIG_OKOU_MICROPYTHON_RUNTIME
#include "esp_netif_sntp.h"
static void sntp_task(void *arg)
{
    /* Validate TLS certificate dates after a cold boot. */
    while (!okou_net_has_ip()) {
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

esp_err_t okou_agent_start(const okou_config_t *cfg)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;
    if (cfg) {
        g_okou_cfg = *cfg;
    }
#if CONFIG_OKL_URL_OTA
    ota_transport = xEventGroupCreate();
    if (!ota_transport) return ESP_ERR_NO_MEM;
#endif
    s_wake = xSemaphoreCreateBinary();
    if (!s_wake) return ESP_ERR_NO_MEM;
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
#if CONFIG_OKL_USB_CDC
        // Keep the USB device active while diagnosing real-hardware enumeration.
        .light_sleep_enable = false,
#else
        .light_sleep_enable = true,
#endif
    };
    esp_err_t pm_err = esp_pm_configure(&pm);
    if (pm_err != ESP_OK) ESP_LOGW(TAG, "Power management unavailable: %s", esp_err_to_name(pm_err));
#endif
    esp_err_t e = okou_nvs_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs init: %s", esp_err_to_name(e));
        return e;
    }
    ensure_queues();
    for (int i = 0; i < 4; ++i) {
        snprintf(s_boot_id + 8*i, 9, "%08" PRIx32, esp_random());
    }
    okou_registry_builtin_cmds();
    s_cfg_ok = load_config();
    e = okou_net_start(g_okou_cfg.manage_network);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "network start: %s", esp_err_to_name(e));
    }
#if CONFIG_OKOU_BLE_WIFI
    if (okou_is_bootloader() && g_okou_cfg.manage_network && e == ESP_OK) okou_ble_init();
#endif
    const esp_app_desc_t *d = esp_app_get_description();
    ESP_LOGI(TAG, "agent start: %s %s role=%s boot_id=%s", d->project_name, d->version,
             okou_is_bootloader() ? "bootloader" : "app", s_boot_id);
    if (xTaskCreate(agent_task, "okou_agent", CONFIG_OKOU_TASK_STACK, NULL, CONFIG_OKOU_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_OKL_URL_OTA || CONFIG_OKOU_MICROPYTHON_RUNTIME
    xTaskCreate(sntp_task, "okl_sntp", 3072, NULL, 3, NULL);
#endif
    return ESP_OK;
}
