#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* One publisher on the agent task, acknowledgements from the MQTT event task. */
bool eh_iot_start(const char *boot_id, const char *hub, const char *token);
bool eh_iot_publish(cJSON *frame, bool confirm);
// 384 raw bytes encode to 512 bytes, plus the bounded MQTT JSON envelope.
#define EH_LOG_CHUNK_BYTES 384
#define EH_LOG_WIRE_BYTES 672
bool eh_iot_publish_log(const uint8_t *bytes, size_t len, uint32_t dropped,
                          char wire[EH_LOG_WIRE_BYTES]);
void eh_iot_stop(void);
unsigned eh_iot_events(void);
enum { IOT_SUBSCRIBED = 1, IOT_OFFLINE = 4 };
cJSON *eh_iot_control(void);
