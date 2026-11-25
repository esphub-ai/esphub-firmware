/* Real SDK + FreeRTOS under Espressif QEMU; deterministic transport, no RF/TLS.
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "okou_priv.h"
#include "okou_iot.h"

static QueueHandle_t wake_queue;
static volatile bool taking, has_ip;
static volatile TickType_t wait_ticks;
static volatile unsigned takes, returns, starts, stops, events, published, commands;
static volatile int64_t start_us, stop_us;
static cJSON *control;
static const char *phase = "startup";

#define CHECK(condition) do { if (!(condition)) { \
    printf("ESPHUB-AGENT-QEMU FAIL %s: %s (line %d)\n", phase, #condition, __LINE__); \
    abort(); \
} } while (0)
#define AWAIT(condition, ms) do { \
    int64_t until = esp_timer_get_time() + (int64_t)(ms) * 1000; \
    while (!(condition) && esp_timer_get_time() < until) vTaskDelay(1); \
    CHECK(condition); \
} while (0)

/* Capture the agent's first (offline) semaphore, then observe only that handle.
 * Other SDK/IDF locks are not mistaken for the agent's wake semaphore. */
BaseType_t __real_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks);
BaseType_t __wrap_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks)
{
    bool agent = !strcmp(pcTaskGetName(NULL), "okou_agent");
    if (agent && !wake_queue) wake_queue = queue;
    bool observe = agent && queue == wake_queue;
    if (observe) {
        wait_ticks = ticks;
        takes++;
        taking = true;
        printf("QEMU-WAIT ticks=%" PRIu32 " max=%" PRIu32 "\n",
               (uint32_t)ticks, (uint32_t)portMAX_DELAY);
    }
    BaseType_t result = __real_xQueueSemaphoreTake(queue, ticks);
    if (observe) { taking = false; returns++; }
    return result;
}

esp_err_t __wrap_okou_net_start(bool manage) { (void)manage; return ESP_OK; }
bool __wrap_okou_net_has_ip(void) { return has_ip; }
bool __wrap_okou_iot_start(const char *boot, const char *hub, const char *token)
{
    CHECK(boot && *boot && !strcmp(hub, "qemu.invalid"));
    CHECK(!strcmp(token, "okl_dev_qemu_dummy_not_a_credential"));
    start_us = esp_timer_get_time();
    starts++;
    return true;
}
void __wrap_okou_iot_stop(void) { stop_us = esp_timer_get_time(); stops++; }
unsigned __wrap_okou_iot_events(void) { unsigned value = events; events = 0; return value; }
cJSON *__wrap_okou_iot_control(void) { cJSON *value = control; control = NULL; return value; }
bool __wrap_okou_iot_publish(cJSON *frame, bool confirm)
{
    (void)confirm;
    CHECK(!strcmp(pcTaskGetName(NULL), "okou_agent"));
    CHECK(cJSON_IsObject(frame));
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "t"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(frame, "name"));
    if (type && !strcmp(type, "event") && name && !strcmp(name, "qemu.queued")) published++;
    if (type && !strcmp(type, "res")) {
        CHECK(cJSON_IsTrue(cJSON_GetObjectItem(frame, "ok")));
        published++;
    }
    return true;
}
static int probe_command(int argc, char **argv, okou_out_t *out)
{
    CHECK(argc == 1 && !strcmp(argv[0], "qemu.probe"));
    CHECK(!strcmp(pcTaskGetName(NULL), "okou_agent"));
    commands++;
    okou_outf(out, "real SDK command dispatch");
    return 0;
}
static void idle(const char *name)
{
    phase = name;
    AWAIT(taking && wait_ticks == portMAX_DELAY, 300);
    unsigned before = takes, resumed = returns;
    vTaskDelay(pdMS_TO_TICKS(120));
    CHECK(taking && wait_ticks == portMAX_DELAY && takes == before && returns == resumed);
    printf("ESPHUB-AGENT-QEMU PASS %s infinite_no_poll\n", name);
}
static void subscribed(void)
{
    events = IOT_SUBSCRIBED;
    okou_agent_notify();
    AWAIT(okou_connected(), 300);
    idle("online_idle");
}
void app_main(void)
{
    CHECK(okou_nvs_init() == ESP_OK);
    CHECK(okou_nvs_set_str("hub", "qemu.invalid") == ESP_OK);
    CHECK(okou_nvs_set_str(OKOU_KEY_TOKEN, "okl_dev_qemu_dummy_not_a_credential") == ESP_OK);
    CHECK(okou_cmd_register("qemu.probe", "simulator probe", probe_command) == ESP_OK);
    okou_config_t cfg = OKOU_CONFIG_DEFAULT();
    CHECK(okou_agent_start(&cfg) == ESP_OK);

    idle("initial_offline");
    CHECK(!starts && !okou_connected());
    phase = "net_ip_wakes";
    has_ip = true;
    okou_agent_notify();
    AWAIT(starts == 1 && taking && wait_ticks != portMAX_DELAY, 300);
    CHECK(wait_ticks > pdMS_TO_TICKS(29000) && wait_ticks <= pdMS_TO_TICKS(30001));
    printf("ESPHUB-AGENT-QEMU PASS net_ip_wakes connecting_deadline\n");
    subscribed();

    phase = "queued_event_wakes";
    unsigned before = returns;
    okou_event("qemu.queued", "{\"source\":\"probe\"}");
    AWAIT(published == 1 && returns > before, 300);
    idle("queued_event_wakes");

    phase = "mqtt_command_wakes";
    control = cJSON_Parse("{\"command\":{\"id\":\"00000000-0000-4000-8000-000000000001\",\"seq\":1,\"op\":\"cmd\",\"args\":{\"name\":\"qemu.probe\",\"argv\":[]}}}");
    CHECK(control);
    okou_agent_notify();
    AWAIT(commands == 1 && published == 2, 300);
    idle("mqtt_command_wakes");

    phase = "disconnect_backoff";
    events = IOT_OFFLINE;
    okou_agent_notify();
    AWAIT(stops == 1 && taking && wait_ticks != portMAX_DELAY, 300);
    CHECK(!okou_connected() && starts == 1);
    TickType_t backoff_ticks = wait_ticks;
    CHECK(backoff_ticks >= pdMS_TO_TICKS(750) && backoff_ticks <= pdMS_TO_TICKS(1201));
    /* No notification is sent here: real FreeRTOS timeout must restart transport. */
    unsigned backoff_returns = returns;
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(starts == 1 && returns == backoff_returns && taking);
    AWAIT(starts == 2, 1600);
    int64_t elapsed_ms = (start_us - stop_us) / 1000;
    CHECK(elapsed_ms >= 799 && elapsed_ms <= 1250);
    printf("ESPHUB-AGENT-QEMU PASS disconnect_backoff deadline_reconnect elapsed_ms=%" PRId64
           " wait_ticks=%" PRIu32 "\n", elapsed_ms, (uint32_t)backoff_ticks);
    subscribed();
    printf("ESPHUB-AGENT-QEMU PASS ALL starts=%u stops=%u commands=%u published=%u\n",
           starts, stops, commands, published);
    /* Host runner checks for panics for another 200 ms before terminating QEMU. */
    vTaskDelete(NULL);
}
