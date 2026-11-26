/*
 * USB-serial provisioning: a local console line `EH-PROVISION <json>` stores the
 * device token and BLE bootstrap key after the first USB flash. The line is never echoed or logged. Physical USB access already implies
 * full control of the device (esptool can rewrite flash), so this adds no new exposure.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "eh_agent_ext.h"
#include "bootloader.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

static const char *TAG = "prov_serial";
#define PREFIX "EH-PROVISION "
#define WIFI_PREFIX "EH-WIFI "
#define PROV_LINE_MAX 640

/* Handle commands typed on the local USB console. */
static void console_line(char *line)
{
    char *save = NULL;
    char *name = strtok_r(line, " \t\r\n", &save);
    if (!name) return;
    cJSON *args = cJSON_CreateArray();
    if (!args) return;
    char *arg;
    while ((arg = strtok_r(NULL, " \t\r\n", &save))) cJSON_AddItemToArray(args, cJSON_CreateString(arg));
    char *output = NULL;
    int rc = eh_cmd_run(name, args, &output);
    const char *message = output ? output : (rc == 0 ? "OK" : "Unknown command. Type help.");
    printf("%s\n", message);
    free(output); cJSON_Delete(args);
}

/* Use JSON so spaces in SSIDs and passwords survive the serial transport. */
static void wifi_line(const char *payload, bool overflow)
{
    cJSON *request = overflow ? NULL : cJSON_ParseWithOpts(payload, NULL, true);
    cJSON *reply = cJSON_CreateObject();
    if (reply) {
        if (cJSON_IsObject(request)) {
            eh_wifi_setup_command(request, reply);
            const char *op = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "op"));
            if (op) cJSON_AddStringToObject(reply, "op", op);
            uint8_t mac[6];
            if (op && !strcmp(op, "wifi.status") && esp_efuse_mac_get_default(mac) == ESP_OK) {
                cJSON_AddStringToObject(reply, "protocol", "esphub-usb-wifi-v2");
                char address[18];
                snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                cJSON_AddStringToObject(reply, "mac", address);
            }
        } else
            cJSON_AddStringToObject(reply, "error", "invalid Wi-Fi request");
    }
    char *result = cJSON_PrintUnformatted(reply);
    printf(WIFI_PREFIX "%s\n", result ? result : "{\"error\":\"out of memory\"}");
    fflush(stdout);
    free(result);
    cJSON_Delete(reply);
    cJSON_Delete(request);
}

static void serial_task(void *arg)
{
    static char line[PROV_LINE_MAX];
    size_t len = 0;
    bool overflow = false;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (len < PROV_LINE_MAX - 1) {
                line[len++] = (char)c;
            } else {
                overflow = true;
            }
            continue;
        }
        line[len] = '\0';
        if (strncmp(line, PREFIX, strlen(PREFIX)) == 0) {
            char err[96] = "";
            if (overflow) {
                printf("EH-PROVISION-ERR line too long\n");
            } else if (prov_apply(line + strlen(PREFIX), err, sizeof(err))) {
                ESP_LOGI(TAG, "provisioning data stored (usb serial)");
                char address[18];
                if (eh_ble_address(address))
                    printf("EH-PROVISION-OK {\"ble_address\":\"%s\"}\n", address);
                else
                    puts("EH-PROVISION-OK {}");
                fflush(stdout);
                eh_net_reconfigure();
                eh_agent_reload_config();
            } else {
                printf("EH-PROVISION-ERR %s\n", err);
            }
        } else if (strncmp(line, WIFI_PREFIX, strlen(WIFI_PREFIX)) == 0) {
            wifi_line(line + strlen(WIFI_PREFIX), overflow);
        } else if (!overflow) {
            console_line(line);
        }
        memset(line, 0, sizeof(line));
        len = 0;
        overflow = false;
    }
}

void prov_serial_start(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "usb serial driver install failed; serial provisioning unavailable");
        return;
    }
    usb_serial_jtag_vfs_use_driver();
#elif CONFIG_ESP_CONSOLE_UART
    if (uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "uart driver install failed; serial provisioning unavailable");
        return;
    }
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#else
    return;
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    xTaskCreate(serial_task, "eh_serial", 4096, NULL, 2, NULL);
}
