/*
 * eh_agent - the EspHub SDK, the device side of the Hub protocol.
 *
 * Link this component into an app and call eh_agent_start().
 * Events enter a bounded queue and a low-priority agent task sends them.
 * A full queue drops the event and increments the drop counter.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define EH_SDK_VERSION "8.1.0"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* true (default): the SDK brings up the network itself (Wi-Fi STA from NVS eh/ssid+pass,
     * falling back to credentials already stored by the Wi-Fi driver; or OpenCores Ethernet
     * when CONFIG_EH_NET_OPENETH=y). false: the app owns esp_netif/Wi-Fi and the SDK just
     * waits for an IP address. */
    bool manage_network;
} eh_config_t;

#define EH_CONFIG_DEFAULT() { .manage_network = true }

/* Start the SDK with the default network configuration. */
esp_err_t eh_agent_start(void);
esp_err_t eh_agent_start_with_config(const eh_config_t *cfg);

/* True while the hub MQTT session is up and our hello has been acknowledged. */
/* Best-effort JSON messages. A successful send only hands the payload to the
 * current session's local queue. reply_to is NULL or a received message ID. */
bool eh_message_send(const char *json, const char *reply_to);
/* Non-blocking; returns a heap-owned JSON envelope or NULL. Free after use. */
char *eh_message_receive(void);

bool eh_connected(void);

/* Reboot into the bootloader and hold it for this boot. Does not return on success. */
esp_err_t eh_enter_bootloader(void);

/* Transfer the Wi-Fi driver between the SDK station and the application.
 * false waits for SDK radio operations to finish, stops/deinitializes the SDK
 * station and leaves saved credentials intact. The app can then initialize Wi-Fi.
 * Before passing true, stop/deinitialize app-owned Wi-Fi and release its default
 * netifs. true starts the SDK station and resumes the saved-network policy.
 * Call from an application task after agent startup, outside IDF event handlers.
 * Idempotent. Applies to this boot only; choose manage_network at next startup.
 * The Agent and other network transports continue running. */
esp_err_t eh_wifi_set_managed(bool enabled);

/* Queue a Wi-Fi trial on the SDK-managed station. Returns false if the input is
 * invalid, another trial is active, or SDK-managed Wi-Fi is unavailable.
 * The manager saves the network only after it obtains an IP address. */
bool eh_wifi_join(const char *ssid, const char *password, bool hidden);
/* "idle", "joining", "saved", "failed", or "unavailable". */
const char *eh_wifi_join_state(void);

/* ---- commands ---------------------------------------------------------------------- */
typedef struct eh_out eh_out_t;
/* argv[0] is the command name. Return 0 for success; non-zero makes the res ok:false with
 * the handler output (or "exit <rc>") as error. Output is capped at CONFIG_EH_CMD_OUT_MAX. */
typedef int (*eh_cmd_handler_t)(int argc, char **argv, eh_out_t *out);
int eh_outf(eh_out_t *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
esp_err_t eh_cmd_register(const char *name, const char *help, eh_cmd_handler_t handler);

/* ---- telemetry ----------------------------------------------------------------------- */
/* json: a JSON object text (or NULL for {}). Invalid JSON is sent as {"text":"..."}.
 * Never blocks; dropped (and counted) when the outbound queue is full. */
void eh_event(const char *name, const char *json);

#ifdef __cplusplus
}
#endif
