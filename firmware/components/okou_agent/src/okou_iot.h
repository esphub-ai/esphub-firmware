#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* One publisher on the agent task, acknowledgements from the MQTT event task. */
bool okou_iot_start(const char *boot_id, const char *hub, const char *token);
bool okou_iot_publish(cJSON *frame, bool confirm);
// 384 raw bytes encode to 512 bytes, plus the bounded MQTT JSON envelope.
#define OKOU_LOG_CHUNK_BYTES 384
#define OKOU_LOG_WIRE_BYTES 672
bool okou_iot_publish_log(const uint8_t *bytes, size_t len, uint32_t dropped,
                          char wire[OKOU_LOG_WIRE_BYTES]);
void okou_iot_stop(void);
unsigned okou_iot_events(void);
enum { IOT_SUBSCRIBED = 1, IOT_OFFLINE = 4 };
cJSON *okou_iot_control(void);
