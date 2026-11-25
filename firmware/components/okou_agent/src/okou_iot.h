#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>

/* One publisher on the agent task, acknowledgements from the MQTT event task. */
bool okou_iot_start(const char *boot_id, const char *hub, const char *token);
bool okou_iot_publish(cJSON *frame, bool confirm);
void okou_iot_stop(void);
unsigned okou_iot_events(void);
enum { IOT_SUBSCRIBED = 1, IOT_OFFLINE = 4 };
cJSON *okou_iot_control(void);
