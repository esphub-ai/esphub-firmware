/*
 * okou_agent extension API - used by the esp-bootloader firmware itself. Apps should not
 * need anything in here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "okou_hub.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public Bluetooth controller address used for provisioning discovery. */
bool okou_ble_address(char out[18]);

/* Signal strength of the current station connection, in dBm. */
bool okou_wifi_rssi(int *rssi);

/* NVS namespace and keys shared between bootloader and SDK (docs/protocol.md). */
#define OKOU_NVS_NS          "okl"
#define OKOU_KEY_PENDING     "pending"    /* str: in-progress transfer identity or unacknowledged final event */
#define OKOU_KEY_LSLOT "lslot" /* Last validated A/B bootloader slot. */
#define OKOU_KEY_HOLD        "hold"       /* u8: one-shot "stay in bootloader on next boot" (extension) */
#define OKOU_KEY_WIFIS       "wifis"      /* str: saved Wi-Fi networks, JSON, most recent first */
#define OKOU_KEY_TOKEN       "tok"        /* str: hub device token okl_dev_... (HTTPS bearer) */

/* Verify the app image and select its partition for the next boot. */
esp_err_t okou_select_app(void);

/* Called by the bootloader download worker, never by the MQTT agent task. */
void okou_pause_mqtt(void);
void okou_resume_mqtt(void);
void okou_install_result_ready(void);

/* Custom op handler. Fill `data` (a cJSON object, owned by the caller) and return ESP_OK,
 * or write a message into err and return an error. Runs on the agent task: must be quick. */
typedef esp_err_t (*okou_op_handler_t)(const cJSON *args, cJSON *data, char *err, size_t errlen);
/* Registers or overrides an op (e.g. the bootloader overrides "install"). */
esp_err_t okou_op_register(const char *op, okou_op_handler_t handler);

/* Deferred op: for work that runs on another task (e.g. an interpreter). Return ESP_OK to keep
 * `req` and answer later, from any task, with okou_op_reply() (exactly once; it frees req and
 * takes ownership of data). Return an error (with err filled) to answer at once. The hub waits
 * up to the request's timeout_ms (max 60 s); a later reply is dropped by the hub. */
typedef struct okou_req okou_req_t;
typedef esp_err_t (*okou_op_async_handler_t)(okou_req_t *req, const cJSON *args, char *err, size_t errlen);
esp_err_t okou_op_register_async(const char *op, okou_op_async_handler_t handler);
void okou_op_reply(okou_req_t *req, bool ok, cJSON *data, const char *err);

int okou_cmd_run(const char *name, const cJSON *argv, char **out_text);

/* State string reported in hello/status pending_install.state (default "queued"). */
void okou_set_install_state(const char *state);

/* Queue an event; takes ownership of data (may be NULL). Never blocks. */
void okou_event_json(const char *name, cJSON *data);

/* Find the bl partition. */
const esp_partition_t *okou_bootloader_partition(void);

/* Builds https://<host> from the registered plain hostname. */
esp_err_t okou_hub_http_base(char *buf, size_t len);
/* Adds the registered device bearer token to a hub HTTPS request. */
void okou_http_apply_auth(esp_http_client_handle_t client);

/* Graceful reboot from the agent task after `delay_ms` (drains queued events). */
void okou_schedule_reboot(uint32_t delay_ms);

/* Re-read the USB registration and reconnect (used by USB provisioning). */
void okou_agent_reload_config(void);
bool okou_net_has_ip(void);
void okou_net_reconfigure(void); /* re-read saved Wi-Fi networks and reconnect */

/* ---- Wi-Fi networks (SDK-managed networking only) ---- */
#define OKOU_WIFI_MAX 5
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    int auth; /* wifi_auth_mode_t */
    uint8_t bssid[6]; /* strongest AP; connection hint for this scan only */
} okou_ap_t;
/* Save (or move to the front) a network; up to OKOU_WIFI_MAX, oldest dropped. */
esp_err_t okou_wifi_add(const char *ssid, const char *pass, bool hidden);
esp_err_t okou_wifi_forget(const char *ssid);
int okou_wifi_count(void);
/* Appends {ssid, hidden, open, current} objects (no passwords). */
void okou_wifi_saved_json(cJSON *arr);
/* Blocking scan (~2-3 s), serialised with connection attempts; one entry per SSID, strongest first. */
int okou_wifi_scan(okou_ap_t *out, int max, uint32_t timeout_ms);
bool okou_wifi_current(char *ssid, size_t len);
const char *okou_wifi_auth_str(int auth);
void okou_wifi_setup_command(cJSON *request, cJSON *reply);
bool okou_mqtt_connected(void); /* socket up (hello may not be acked yet) */

/* NVS helpers (namespace okl). */
esp_err_t okou_nvs_init(void);
esp_err_t okou_nvs_get_str(const char *key, char *buf, size_t len);
char *okou_nvs_get_str_alloc(const char *key); /* malloc'd or NULL */
esp_err_t okou_nvs_set_str(const char *key, const char *val);
uint8_t okou_nvs_get_u8(const char *key, uint8_t def);
esp_err_t okou_nvs_set_u8(const char *key, uint8_t val);
esp_err_t okou_nvs_erase(const char *key);

#ifdef __cplusplus
}
#endif
