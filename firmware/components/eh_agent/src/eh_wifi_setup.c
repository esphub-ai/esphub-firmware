#include <string.h>
#include "eh_agent.h"
#include "eh_agent_ext.h"

void eh_wifi_setup_command(cJSON *request, cJSON *reply)
{
    const char *op = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "op"));
    if (op && !strcmp(op, "wifi.scan")) {
        eh_ap_t aps[20];
        int n = eh_wifi_scan(aps, 20, 20000);
        if (n < 0) {
            cJSON_AddStringToObject(reply, "error", "scan busy");
            return;
        }
        cJSON *list = cJSON_AddArrayToObject(reply, "ssids");
        for (int i = 0; i < n; i++)
            cJSON_AddItemToArray(list, cJSON_CreateString(aps[i].ssid));
    } else if (op && !strcmp(op, "wifi.join")) {
        const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "ssid"));
        const char *pass = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "password"));
        if (!pass ||
            !eh_wifi_join(ssid, pass, cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "hidden"))))
            cJSON_AddStringToObject(reply, "error", "invalid network");
        else
            cJSON_AddBoolToObject(reply, "joining", true);
    } else if (op && !strcmp(op, "wifi.status")) {
        char current[33];
        cJSON_AddStringToObject(reply, "state", eh_wifi_join_state());
        cJSON_AddStringToObject(reply, "connected",
                                eh_wifi_current(current, sizeof(current)) ? current : "");
        cJSON_AddBoolToObject(reply, "hub", eh_mqtt_connected());
    } else
        cJSON_AddStringToObject(reply, "error", "unknown operation");
}
