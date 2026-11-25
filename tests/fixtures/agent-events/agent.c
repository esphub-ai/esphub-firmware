/* Run the real task and JSON dispatcher; fake only SDK, clock and transport. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "fake-idf.h"
#include "firmware/components/okou_agent/src/okou_agent.c"

struct fake_queue { unsigned cap, size, count, head; unsigned char *items; };
struct fake_semaphore { bool pending; };
struct fake_event_group { EventBits_t bits; };
static jmp_buf done;
static const char *scenario;
static int64_t clock_us = 1000000, events_cost_us, publish_cost_us, start_cost_us, stop_cost_us;
static bool has_ip = true, config_valid = true, start_ok = true, in_isr;
static bool fail_result, fail_event, fail_hello, race_injected;
static unsigned waits, starts, stops, hellos, event_count, results, commands, isr_gives;
static unsigned transport_events, event_polls, reboot_expected_polls;
static int64_t expected_reboot;
static void (*task_entry)(void *);
static cJSON *control;
static char *pending_install;
static unsigned pending_erases, final_events;
static const esp_partition_t partition = { .label = "app" };
static const esp_app_desc_t descriptor = { .project_name = "native-test", .version = "1" };

static bool is(const char *name) { return !strcmp(scenario, name); }
static void finish(void) { longjmp(done, 1); }
static void forever(TickType_t ticks) { assert(ticks == portMAX_DELAY); }
static void deadline(TickType_t ticks, int64_t at)
{
    /* A real RTOS rounds to ticks; never early and never > one tick late. */
    assert(ticks != portMAX_DELAY);
    int64_t delay = (int64_t)ticks * portTICK_PERIOD_MS * 1000;
    assert(delay >= at - clock_us && delay <= at - clock_us + portTICK_PERIOD_MS * 1000);
}
static void subscribed(void) { transport_events |= IOT_SUBSCRIBED; okou_agent_notify(); }
static void disconnected(void) { transport_events |= IOT_OFFLINE; okou_agent_notify(); }
static void queue_command(void)
{
    assert(!control);
    control = cJSON_Parse("{\"command\":{\"id\":\"11111111-2222-3333-4444-555555555555\",\"seq\":1,\"op\":\"echo\",\"args\":{\"value\":42}}}");
    assert(control);
    okou_agent_notify();
}
static esp_err_t echo(const cJSON *args, cJSON *data, char *err, size_t len)
{
    assert(cJSON_GetObjectItem(args, "value")->valueint == 42);
    ++commands;
    cJSON_AddNumberToObject(data, "answer", 42);
    return ESP_OK;
}

/* Called only when the real binary semaphore would block. Each callback either
 * delivers an external event, advances virtual time to a deadline, or exits. */
static void on_wait(TickType_t ticks)
{
    unsigned step = waits++;
    assert(waits < 20); /* deterministic runaway guard, in addition to runner timeout */
    if (is("offline-no-ip") || is("unconfigured")) {
        forever(ticks); assert(starts == 0); finish();
    }
    if (is("boot-ip-wake")) {
        if (step == 0) { forever(ticks); assert(starts == 0); has_ip = true; okou_agent_notify(); }
        else if (step == 1) { deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US); subscribed(); }
        else { forever(ticks); assert(okou_connected() && hellos == 1); finish(); }
        return;
    }
    if (is("slow-enroll-clock")) {
        assert(step == 0 && starts == 1 && stops == 1 && !s_iot_started);
        assert(clock_us == 1000000 + CONNECT_TIMEOUT_US);
        deadline(ticks, s_next_connect_us); finish();
    }
    if (is("connecting-network-disconnect")) {
        if (step == 0) {
            deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US);
            has_ip = false; okou_agent_notify();
        } else { forever(ticks); assert(stops == 1 && !s_iot_started); finish(); }
        return;
    }
    if (is("connecting-reboot")) {
        if (step == 0) {
            deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US);
            okou_schedule_reboot(125); expected_reboot = s_reboot_at_us;
        } else { deadline(ticks, expected_reboot); clock_us = expected_reboot; }
        return;
    }
    if (is("retry-before-reboot")) {
        if (step == 0) {
            assert(starts == 1 && !s_iot_started);
            deadline(ticks, s_next_connect_us);
            okou_schedule_reboot(2000); expected_reboot = s_reboot_at_us;
        } else if (step == 1) {
            deadline(ticks, s_next_connect_us); clock_us = s_next_connect_us; start_ok = true;
        } else {
            assert(starts == 2 && s_iot_started);
            deadline(ticks, expected_reboot); clock_us = expected_reboot;
        }
        return;
    }
    if (is("connect-timeout") || is("fresh-connect-timeout")) {
        if (step == 0) {
            deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US);
            if (is("fresh-connect-timeout")) { events_cost_us = CONNECT_TIMEOUT_US; okou_agent_notify(); }
            else clock_us = s_conn_start_us + CONNECT_TIMEOUT_US;
        } else {
            assert(stops == 1 && !s_iot_started);
            deadline(ticks, clock_us + 1000000);
            assert(s_next_connect_us == clock_us + 1000000);
            finish();
        }
        return;
    }
    if (is("reconnect-deadline")) {
        if (step == 0) {
            assert(starts == 1 && !s_iot_started);
            deadline(ticks, clock_us + 1000000);
            clock_us = s_next_connect_us; start_ok = true;
        } else {
            assert(starts == 2 && s_iot_started);
            deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US); finish();
        }
        return;
    }
    if (is("largest-reboot") && step == 0) {
        assert(portTICK_PERIOD_MS == 1 && ticks == portMAX_DELAY - 1);
        clock_us += (int64_t)ticks * 1000;
        assert(clock_us < expected_reboot); /* saturated wait must not reboot early */
        return;
    }
    if (is("offline-reboot") || is("sub-tick-reboot") || is("largest-reboot")) {
        deadline(ticks, expected_reboot); assert(starts == 0);
        clock_us = expected_reboot; return;
    }
    /* All remaining scenarios establish their session through the real task. */
    if (step == 0) {
        deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US);
        assert(starts == 1); subscribed(); return;
    }
    assert(hellos >= 1);
    --step;
    if (is("idle-online")) {
        forever(ticks); assert(okou_connected()); finish();
    } else if (is("queued-events")) {
        forever(ticks);
        if (step == 0) { okou_event("first", "{\"n\":1}"); okou_event("second", "{\"n\":2}"); }
        else { assert(event_count == 2); finish(); }
    } else if (is("wake-latch")) {
        forever(ticks); assert(race_injected && event_count == 1 && isr_gives == 2); finish();
    } else if (is("mqtt-command")) {
        forever(ticks);
        if (step == 0) queue_command();
        else { assert(commands == 1 && results == 1 && !s_last_receipt); finish(); }
    } else if (is("command-failure-recovery")) {
        if (step == 0) { forever(ticks); fail_result = true; queue_command(); }
        else if (step == 1) {
            assert(stops == 1 && commands == 1 && results == 1 && s_last_receipt);
            deadline(ticks, s_next_connect_us); clock_us = s_next_connect_us;
        } else if (step == 2) {
            assert(starts == 2); deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US);
            subscribed(); queue_command();
        } else {
            forever(ticks); assert(commands == 1 && results == 2 && !s_last_receipt); finish();
        }
    } else if (is("hello-failure-recovery")) {
        assert(step == 0 && stops == 1 && !s_iot_started && !okou_connected());
        deadline(ticks, s_next_connect_us); finish();
    } else if (is("event-failure-recovery")) {
        if (step == 0) { forever(ticks); fail_event = true; okou_event("failed", "{}"); }
        else { assert(stops == 1 && !s_iot_started && event_count == 1); deadline(ticks, s_next_connect_us); finish(); }
    } else if (is("network-disconnect")) {
        if (step == 0) { forever(ticks); has_ip = false; okou_agent_notify(); }
        else { forever(ticks); assert(stops == 1 && !s_iot_started); finish(); }
    } else if (is("mqtt-disconnect")) {
        if (step == 0) { forever(ticks); disconnected(); }
        else { assert(stops == 1); deadline(ticks, s_next_connect_us); finish(); }
    } else if (is("stable-online-reset") || is("short-online-backoff")) {
        if (step == 0) {
            /* Large backoff must not cause a stable-online housekeeping timer. */
            forever(ticks); assert(s_backoff_ms == 32000);
            clock_us = s_up_since_us + (is("stable-online-reset") ? BACKOFF_RESET_US : BACKOFF_RESET_US - 1);
            stop_cost_us = 2000000; /* teardown must not turn a short session into a stable one */
            disconnected();
        } else {
            int64_t retry_us = is("stable-online-reset") ? 1000000 : 32000000;
            assert(stops == 1 && s_next_connect_us == clock_us + retry_us);
            deadline(ticks, s_next_connect_us); finish();
        }
    } else if (is("reboot-deadline")) {
        if (step == 0) { forever(ticks); okou_schedule_reboot(250); expected_reboot = s_reboot_at_us; }
        else { deadline(ticks, expected_reboot); clock_us = expected_reboot; }
    } else if (is("fresh-reboot") || is("failed-publish-reboot-priority")) {
        forever(ticks); assert(step == 0);
        publish_cost_us = 800000; okou_schedule_reboot(300); expected_reboot = s_reboot_at_us;
        reboot_expected_polls = event_polls + 1;
        fail_event = is("failed-publish-reboot-priority"); okou_event("slow-publish", "{}");
#if CONFIG_OKL_URL_OTA
    } else if (is("ota-pause-resume") || is("ota-paused-reboot")) {
        if (step == 0) {
            forever(ticks); xEventGroupSetBits(ota_transport, OTA_PAUSE); okou_agent_notify();
            if (is("ota-paused-reboot")) { okou_schedule_reboot(300); expected_reboot = s_reboot_at_us; }
        } else if (step == 1) {
            assert(stops == 1 && (xEventGroupGetBits(ota_transport) & MQTT_STOPPED));
            if (is("ota-paused-reboot")) { deadline(ticks, expected_reboot); clock_us = expected_reboot; }
            else { forever(ticks); okou_resume_mqtt(); }
        } else if (step == 2) {
            assert(starts == 2); deadline(ticks, s_conn_start_us + CONNECT_TIMEOUT_US); subscribed();
        } else { forever(ticks); assert(okou_connected() && hellos == 2); finish(); }
    } else if (is("ota-result-wake")) {
        forever(ticks);
        if (step == 0) {
            pending_install = strdup("{\"t\":\"event\",\"name\":\"install.done\",\"data\":{}}");
            okou_install_result_ready();
        } else { assert(final_events == 1 && pending_erases == 1 && okou_connected()); finish(); }
#endif
    } else { fprintf(stderr, "unknown scenario: %s\n", scenario); abort(); }
}

BaseType_t xPortInIsrContext(void) { return in_isr; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return calloc(1, sizeof(struct fake_semaphore)); }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { s->pending = true; return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *higher)
{ ++isr_gives; *higher = pdFALSE; return xSemaphoreGive(s); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    assert(s == s_wake);
    /* Producer runs in the state-check/sleep gap, with repeated ISR notifications
     * coalescing into one latched token. Task must recheck and drain its queue. */
    if (is("wake-latch") && s_mqtt_up && !race_injected) {
        race_injected = true; in_isr = true;
        okou_event("race", "{}"); okou_agent_notify(); in_isr = false;
    }
    if (s->pending) { s->pending = false; return pdTRUE; }
    on_wait(ticks);
    if (s->pending) { s->pending = false; return pdTRUE; }
    return pdFALSE;
}
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size)
{
    QueueHandle_t q = calloc(1, sizeof(*q)); assert(q);
    q->cap = capacity; q->size = size; q->items = calloc(capacity, size); assert(q->items); return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    assert(ticks == 0); if (q->count == q->cap) return pdFALSE;
    memcpy(q->items + ((q->head + q->count++) % q->cap) * q->size, item, q->size); return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    assert(ticks == 0); if (!q->count) return pdFALSE;
    memcpy(item, q->items + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->cap; --q->count; return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { free(q->items); free(q); }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, UBaseType_t prio, void *handle)
{ if (!strcmp(name, "okou_agent")) { assert(!task_entry); task_entry = fn; } return pdPASS; }
void vTaskDelay(TickType_t ticks) { clock_us += (int64_t)ticks * portTICK_PERIOD_MS * 1000; }
void vTaskDelete(void *task) { abort(); }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1, sizeof(struct fake_event_group)); }
EventBits_t xEventGroupGetBits(EventGroupHandle_t g) { return g->bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits) { return g->bits |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits)
{ EventBits_t old = g->bits; g->bits &= ~bits; return old; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear, BaseType_t all, TickType_t ticks)
{ assert((g->bits & bits) == bits); EventBits_t old = g->bits; if (clear) g->bits &= ~bits; return old; }

bool okou_iot_start(const char *boot_id, const char *hub, const char *token)
{ assert(*boot_id && !strcmp(hub, "hub.test") && !strcmp(token, "token")); ++starts; clock_us += start_cost_us; start_cost_us = 0; return start_ok; }
void okou_iot_stop(void) { ++stops; clock_us += stop_cost_us; stop_cost_us = 0; }
unsigned okou_iot_events(void)
{
    ++event_polls; clock_us += events_cost_us; events_cost_us = 0;
    unsigned events = transport_events; transport_events = 0; return events;
}
cJSON *okou_iot_control(void) { cJSON *frame = control; control = NULL; return frame; }
bool okou_iot_publish(cJSON *frame, bool confirm)
{
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "t")); assert(type);
    clock_us += publish_cost_us; publish_cost_us = 0;
    if (!strcmp(type, "hello")) {
        ++hellos; assert(confirm);
        if (fail_hello) { fail_hello = false; return false; }
    }
    else if (!strcmp(type, "res")) {
        ++results; assert(confirm && cJSON_IsTrue(cJSON_GetObjectItem(frame, "ok")));
        assert(cJSON_GetObjectItem(cJSON_GetObjectItem(frame, "data"), "answer")->valueint == 42);
        if (fail_result) { fail_result = false; return false; }
    } else if (!strcmp(type, "event")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "name")); assert(name);
        if (!strcmp(name, "install.done")) { ++final_events; assert(confirm); }
        else {
            ++event_count; assert(!confirm);
            if (is("queued-events")) {
                assert(!strcmp(name, event_count == 1 ? "first" : "second"));
                assert(cJSON_GetObjectItem(cJSON_GetObjectItem(frame, "data"), "n")->valueint == (int)event_count);
            }
            if (fail_event) { fail_event = false; return false; }
        }
    } else abort();
    return true;
}

int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { return 20; } /* exactly 100% reconnect jitter */
void esp_restart(void)
{
    assert(expected_reboot && clock_us >= expected_reboot);
    if (is("fresh-reboot") || is("failed-publish-reboot-priority")) {
        assert(event_polls == reboot_expected_polls);
        assert(stops == 0); /* due reboot takes priority over failed-publish recovery */
    }
    longjmp(done, 2);
}
size_t strlcpy(char *dst, const char *src, size_t n)
{ size_t len = strlen(src); if (n) { size_t copy = len < n - 1 ? len : n - 1; memcpy(dst, src, copy); dst[copy] = 0; } return len; }
const char *esp_err_to_name(esp_err_t e) { return "fake-error"; }
esp_err_t okou_nvs_init(void) { return ESP_OK; }
esp_err_t okou_nvs_get_str(const char *key, char *buf, size_t len)
{ if (!config_valid) return ESP_ERR_NOT_FOUND; strlcpy(buf, !strcmp(key, "hub") ? "hub.test" : "token", len); return ESP_OK; }
char *okou_nvs_get_str_alloc(const char *key) { return pending_install ? strdup(pending_install) : NULL; }
esp_err_t okou_nvs_set_str(const char *key, const char *value) { return ESP_OK; }
esp_err_t okou_nvs_set_u8(const char *key, uint8_t value) { return ESP_OK; }
esp_err_t okou_nvs_erase(const char *key)
{ if (!strcmp(key, OKOU_KEY_PENDING)) { ++pending_erases; free(pending_install); pending_install = NULL; } return ESP_OK; }
bool okou_hub_host_valid(const char *hub) { return !strcmp(hub, "hub.test"); }
bool okou_net_has_ip(void) { return has_ip; }
esp_err_t okou_net_start(bool manage) { return ESP_OK; }
void okou_registry_builtin_cmds(void) { }
bool okou_ble_address(char out[18]) { return false; }
const esp_partition_t *okou_bootloader_partition(void) { return &partition; }
const esp_partition_t *esp_ota_get_running_partition(void) { return &partition; }
const esp_partition_t *esp_ota_get_boot_partition(void) { return &partition; }
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) { return &partition; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { return ESP_OK; }
const esp_app_desc_t *esp_app_get_description(void) { return &descriptor; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *desc) { *desc = descriptor; return ESP_OK; }
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) { memset(mac, 0, 6); return ESP_OK; }
uint32_t esp_get_free_heap_size(void) { return 100000; }
size_t heap_caps_get_free_size(unsigned caps) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { return 100000; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value) { return ESP_OK; }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) { return ESP_OK; }
const char *okou_reset_reason_str(void) { return "host"; }
const char *okou_ota_state_str(const esp_partition_t *p) { return "valid"; }
void okou_hex(const uint8_t *in, size_t n, char *out) { memset(out, '0', n * 2); out[n * 2] = 0; }
void okou_net_info(cJSON *obj) { }
cJSON *okou_partitions_json(void) { return cJSON_CreateArray(); }
cJSON *okou_cmd_list_json(void) { return cJSON_CreateArray(); }
cJSON *okou_param_list_json(void) { return cJSON_CreateArray(); }
int okou_cmd_run(const char *name, const cJSON *argv, char **out) { return -1000; }
esp_err_t okou_param_get_json(const char *name, cJSON *data, char *err, size_t len) { return ESP_ERR_NOT_FOUND; }
esp_err_t okou_param_set_json(const cJSON *args, cJSON *data, char *err, size_t len) { return ESP_ERR_NOT_FOUND; }

int main(int argc, char **argv)
{
    assert(argc == 2); scenario = argv[1];
    if (is("offline-no-ip") || is("boot-ip-wake") || is("offline-reboot") ||
        is("sub-tick-reboot") || is("largest-reboot")) has_ip = false;
    if (is("unconfigured")) config_valid = false;
    if (is("reconnect-deadline") || is("retry-before-reboot")) start_ok = false;
    if (is("slow-enroll-clock")) start_cost_us = CONNECT_TIMEOUT_US;
    if (is("hello-failure-recovery")) fail_hello = true;
    /* A producer may notify before start; this must be harmless. */
    okou_agent_notify();
    assert(okou_agent_start(NULL) == ESP_OK && task_entry);
    assert(okou_op_register("echo", echo) == ESP_OK);
    if (is("stable-online-reset") || is("short-online-backoff")) s_backoff_ms = 32000;
    if (is("offline-reboot") || is("sub-tick-reboot") || is("largest-reboot")) {
        okou_schedule_reboot(is("largest-reboot") ? UINT32_MAX : is("sub-tick-reboot") ? 1 : 125);
        expected_reboot = s_reboot_at_us;
        if (is("sub-tick-reboot")) clock_us += 999;
    }
    int result = setjmp(done);
    if (!result) { task_entry(NULL); abort(); }
    bool reboot_case = is("reboot-deadline") || is("fresh-reboot") || is("offline-reboot") ||
                       is("ota-paused-reboot") || is("connecting-reboot") || is("retry-before-reboot") ||
                       is("sub-tick-reboot") || is("largest-reboot") || is("failed-publish-reboot-priority");
    assert(result == (reboot_case ? 2 : 1));
    printf("%s: ok\n", scenario);
    return 0;
}
