/*
 * okou_agent - device side of the esp-bootloader protocol.
 *
 * Link this component into an app, register commands/params, call okou_agent_start().
 * Events enter a bounded queue and a low-priority agent task sends them.
 * A full queue drops the event and increments the drop counter.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define OKOU_SDK_VERSION "6.0.0"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OKOU_ROLE_APP = 0,      /* normal apps */
    OKOU_ROLE_BOOTLOADER = 1, /* only the esp-bootloader firmware */
} okou_role_t;

typedef struct {
    okou_role_t role;
    /* true (default): the SDK brings up the network itself (Wi-Fi STA from NVS okl/ssid+pass,
     * falling back to credentials already stored by the Wi-Fi driver; or OpenCores Ethernet
     * when CONFIG_OKOU_NET_OPENETH=y). false: the app owns esp_netif/Wi-Fi and the SDK just
     * waits for an IP address. */
    bool manage_network;
    /* false (default): command handlers run on the agent task.
     * true: `cmd` requests are queued and run inside okou_poll() on the caller's task. */
    bool handlers_in_poll;
    /* Static interpreter version string, set only by the MicroPython port. */
    const char *micropython_version;
} okou_config_t;

#define OKOU_CONFIG_DEFAULT() { .role = OKOU_ROLE_APP, .manage_network = true, .handlers_in_poll = false, .micropython_version = NULL }

/* Start the SDK. cfg may be NULL (defaults). */
esp_err_t okou_agent_start(const okou_config_t *cfg);

/* True while the hub MQTT session is up and our hello has been acknowledged. */
bool okou_connected(void);

/* Reboot into the bootloader and hold it for this boot. Does not return on success. */
esp_err_t okou_enter_bootloader(void);

/* Queue a Wi-Fi trial on the SDK-managed station. Returns false if the input is
 * invalid, another trial is active, or SDK-managed Wi-Fi is unavailable.
 * The manager saves the network only after it obtains an IP address. */
bool okou_wifi_join(const char *ssid, const char *password, bool hidden);
/* "idle", "joining", "saved", "failed", or "unavailable". */
const char *okou_wifi_join_state(void);

/* ---- commands ---------------------------------------------------------------------- */
typedef struct okou_out okou_out_t;
/* argv[0] is the command name. Return 0 for success; non-zero makes the res ok:false with
 * the handler output (or "exit <rc>") as error. Output is capped at CONFIG_OKOU_CMD_OUT_MAX. */
typedef int (*okou_cmd_handler_t)(int argc, char **argv, okou_out_t *out);
int okou_outf(okou_out_t *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
esp_err_t okou_cmd_register(const char *name, const char *help, okou_cmd_handler_t handler);

/* ---- params -------------------------------------------------------------------------- */
/* The variable behind `ptr` is owned by the app and must stay valid forever. The agent
 * writes it with a single aligned 32-bit store when the operator calls param.set.
 * persist=true: value is loaded from NVS (namespace "okp", key = name, max 15 chars) at
 * registration time and saved on param.set. Names are limited to 23 chars. */
esp_err_t okou_param_int(const char *name, int32_t *ptr, int32_t min, int32_t max, bool persist);
esp_err_t okou_param_float(const char *name, float *ptr, float min, float max, bool persist);
esp_err_t okou_param_bool(const char *name, bool *ptr, bool persist);
/* Save the current value of a persistent param after the app changed it itself. */
esp_err_t okou_param_save(const char *name);

/* ---- telemetry ----------------------------------------------------------------------- */
/* json: a JSON object text (or NULL for {}). Invalid JSON is sent as {"text":"..."}.
 * Never blocks; dropped (and counted) when the outbound queue is full. */
void okou_event(const char *name, const char *json);

/* Only meaningful with handlers_in_poll=true: runs queued command handlers on the calling
 * task. Waits up to timeout_ms for work. Returns the number of handlers run. */
int okou_poll(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
