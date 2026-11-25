/*
 * USB-serial provisioning: a local console line `OKL-PROVISION <json>` stores the
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
#include "okou_agent_ext.h"
#include "bootloader.h"
#if CONFIG_OKL_USB_CDC
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

static const char *TAG = "prov_serial";
#define PREFIX "OKL-PROVISION "
#define WIFI_PREFIX "OKL-WIFI "
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
    int rc = okou_cmd_run(name, args, &output);
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
            okou_wifi_setup_command(request, reply);
            const char *op = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, "op"));
            if (op) cJSON_AddStringToObject(reply, "op", op);
            uint8_t mac[6];
            if (op && !strcmp(op, "wifi.status") && esp_efuse_mac_get_default(mac) == ESP_OK) {
                cJSON_AddStringToObject(reply, "protocol", "esphub-usb-wifi-v1");
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
                printf("OKL-PROVISION-ERR line too long\n");
            } else if (prov_apply(line + strlen(PREFIX), err, sizeof(err))) {
                ESP_LOGI(TAG, "provisioning data stored (usb serial)");
                char address[18];
                if (okou_ble_address(address))
                    printf("OKL-PROVISION-OK {\"ble_address\":\"%s\"}\n", address);
                else
                    puts("OKL-PROVISION-OK {}");
                fflush(stdout);
                okou_net_reconfigure();
                okou_agent_reload_config();
            } else {
                printf("OKL-PROVISION-ERR %s\n", err);
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

#if CONFIG_OKL_USB_CDC
static bool usb_cdc_ready;

void prov_usb_cdc_init(void)
{
    ESP_LOGI(TAG, "initializing USB CDC");
    const tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    const tinyusb_config_cdcacm_t cdc = { .cdc_port = TINYUSB_CDC_ACM_0 };
    esp_err_t err = tinyusb_driver_install(&usb);
    if (err == ESP_OK)
        err = tinyusb_cdcacm_init(&cdc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB CDC driver initialization failed: %s", esp_err_to_name(err));
        return;
    }
    usb_cdc_ready = true;
    ESP_LOGI(TAG, "USB CDC driver ready");
}
#endif

void prov_serial_start(void)
{
#if CONFIG_OKL_USB_CDC
    if (!usb_cdc_ready)
        return;
    esp_err_t err = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB CDC console initialization failed: %s", esp_err_to_name(err));
        return;
    }
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
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
    xTaskCreate(serial_task, "okl_serial", 4096, NULL, 2, NULL);
}
