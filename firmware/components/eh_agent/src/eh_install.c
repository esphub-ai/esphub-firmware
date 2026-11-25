/* Streaming HTTPS OTA. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "eh_install.h"
#include "eh_agent.h"
#include "eh_agent_ext.h"
#include "sdkconfig.h"

#define CHUNK 4096
#define URL_LIMIT 1024
#define PROFILE_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

static const char *TAG = "install";
typedef struct {
    char kind[16];
    char url[URL_LIMIT + 1];
    char id[37];
} inst_t;
static volatile bool s_running;

static const esp_partition_t *app_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "app");
}

static bool https_url(const char *url)
{
    if (!url || strncmp(url, "https://", 8) || strpbrk(url, "\r\n\t \\#")) return false;
    const char *host = url + 8;
    size_t len = strcspn(host, "/?");
    return len > 0 && !memchr(host, '@', len);
}

static esp_err_t parse_args(const cJSON *a, inst_t **out, char *err, size_t n)
{
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(a, "kind"));
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(a, "url"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(a, "transfer_id"));
    if (!kind || (strcmp(kind, "bootloader") && strcmp(kind, "app"))) {
        snprintf(err, n, "Expected bootloader or app");
        return ESP_ERR_INVALID_ARG;
    }
    if (!https_url(url) || strlen(url) > URL_LIMIT) {
        snprintf(err, n, "Expected HTTPS URL (up to %u bytes)", (unsigned)URL_LIMIT);
        return ESP_ERR_INVALID_ARG;
    }
    if (!id || strlen(id) != 36) {
        snprintf(err, n, "Invalid transfer ID");
        return ESP_ERR_INVALID_ARG;
    }
    inst_t *in = malloc(sizeof(*in));
    if (!in) {
        snprintf(err, n, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(in->kind, kind, sizeof(in->kind));
    strlcpy(in->url, url, sizeof(in->url));
    strlcpy(in->id, id, sizeof(in->id));
    *out = in;
    return ESP_OK;
}

static cJSON *receipt(const inst_t *in)
{
    cJSON *d = cJSON_CreateObject();
    if (!d || !cJSON_AddStringToObject(d, "kind", in->kind) ||
        !cJSON_AddStringToObject(d, "transfer_id", in->id)) {
        cJSON_Delete(d);
        return NULL;
    }
    return d;
}

// Replace the in-progress marker atomically. Either the marker or its final receipt
// survives power loss. The agent erases the receipt only after the relay ACK.
static bool save_result(const char *name, cJSON *data, const char *target)
{
    cJSON *frame = cJSON_CreateObject();
    char *encoded = NULL;
    if (data && cJSON_AddItemToObject(frame, "data", data)) {
        data = NULL;
        if (cJSON_AddStringToObject(frame, "t", "event") &&
            cJSON_AddStringToObject(frame, "name", name) &&
            (!target || cJSON_AddStringToObject(frame, "target", target)))
            encoded = cJSON_PrintUnformatted(frame);
    }
    bool saved = encoded && eh_nvs_set_str(EH_KEY_PENDING, encoded) == ESP_OK;
    if (saved) eh_install_result_ready();
    else ESP_LOGE(TAG, "Cannot persist terminal install result");
    free(encoded);
    cJSON_Delete(data);
    cJSON_Delete(frame);
    return saved;
}

static void emit_failed(const inst_t *in, const char *msg)
{
    ESP_LOGE(TAG, "%s OTA: %s", in->kind, msg);
    cJSON *d = receipt(in);
    cJSON_AddStringToObject(d, "error", msg);
    save_result("install.failed", d, NULL);
    eh_set_install_state("failed");
}

static esp_err_t on_header(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Location")) {
        *(bool *)e->user_data = https_url(e->header_value) ||
            (e->header_value[0] == '/' && e->header_value[1] != '/' && !strchr(e->header_value, '\\'));
    }
    return ESP_OK;
}

static bool is_private_artifact(const char *url)
{
    char base[192];
    if (eh_hub_http_base(base, sizeof(base)) != ESP_OK) return false;
    size_t n = strlen(base);
    const char prefix[] = "/api/device/artifacts/";
    return !strncmp(url, base, n) && !strncmp(url + n, prefix, sizeof(prefix) - 1);
}

static const char *open_download(esp_http_client_handle_t c, bool private_artifact, bool *safe_redirect,
                                 int64_t *length)
{
    if (private_artifact) eh_http_apply_auth(c);
    for (int hop = 0; hop <= 5; hop++) {
        *safe_redirect = false;
        if (esp_http_client_open(c, 0) != ESP_OK) return "HTTPS connection failed";
        *length = esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (*length < 0) return "Invalid HTTPS response headers";
        if (status == 200) return NULL;
        if (private_artifact || !*safe_redirect || hop == 5 ||
            (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)) {
            return "Download refused or invalid redirect";
        }
        esp_http_client_close(c);
        if (esp_http_client_set_redirection(c) != ESP_OK) return "Invalid redirect URL";
    }
    return "Too many redirects";
}

static bool valid_header(const uint8_t *header, const inst_t *in)
{
    const esp_image_header_t *ih = (const esp_image_header_t *)header;
    if (ih->magic != ESP_IMAGE_HEADER_MAGIC || ih->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) return false;
    static const char bootloader_profile[] = EH_BL_IMAGE_PREFIX CONFIG_EH_BL_PROFILE "\0" EH_OTA_CAPABILITY;
    const uint8_t *profile = header + PROFILE_OFFSET;
    if (!strcmp(in->kind, "bootloader")) return !memcmp(profile, bootloader_profile, sizeof(bootloader_profile));
    return memcmp(profile, EH_BL_IMAGE_PREFIX, sizeof(EH_BL_IMAGE_PREFIX) - 1) != 0;
}

/* Write the firmware header last so a partial image cannot boot. */
static bool run_install(inst_t *in)
{
    bool bootloader = !strcmp(in->kind, "bootloader");
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *target = app_partition();
    if (bootloader) {
        if (!strcmp(run->label, "launcher_a"))
            target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "launcher_b");
        else if (!strcmp(run->label, "launcher_b"))
            target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "launcher_a");
        else
            target = NULL;
    }
    bool private_artifact = is_private_artifact(in->url), safe_redirect = false;
    esp_http_client_config_t config = {
        .url = in->url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 20000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL, .buffer_size = 2048, .buffer_size_tx = 4096,
        .disable_auto_redirect = true, .event_handler = on_header, .user_data = &safe_redirect,
    };
    esp_http_client_handle_t c = esp_http_client_init(&config);
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    uint8_t *buf = malloc(CHUNK), *header = malloc(CHUNK);
    int64_t length = 0;
    uint32_t written = 0;
    const char *fail = NULL;
    if (!c || !buf || !header) { fail = "Out of memory"; goto out; }
    if (!target || target->address == esp_ota_get_running_partition()->address) { fail = "Incompatible partition layout"; goto out; }
    uint32_t limit = target->size;
    eh_set_install_state("downloading");
    fail = open_download(c, private_artifact, &safe_redirect, &length);
    if (fail) goto out;
    if (length > limit) { fail = "Download exceeds target size"; goto out; }
    for (;;) {
        int n = esp_http_client_read(c, (char *)buf, CHUNK);
        if (n < 0) { fail = "HTTPS read failed"; goto out; }
        if (!n) break;
        if ((uint32_t)n > limit - written) { fail = "Download exceeds target size"; goto out; }
        uint32_t off = 0;
        if (written < CHUNK) {
            off = (uint32_t)n < CHUNK - written ? (uint32_t)n : CHUNK - written;
            memcpy(header + written, buf, off);
            if (written + off == CHUNK) {
                if (!valid_header(header, in)) { fail = "Firmware type, chip or bootloader variant mismatch"; goto out; }
                if (esp_ota_begin(target, length > 0 ? (size_t)length : OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
                    fail = "Cannot open target partition"; goto out;
                }
                ota_open = true;
            }
        }
        if ((uint32_t)n > off && esp_ota_write_with_offset(ota, buf + off, n - off, written + off) != ESP_OK) {
            fail = "Flash write failed"; goto out;
        }
        written += n;
    }
    if (!written || (length > 0 && written != length) || !esp_http_client_is_complete_data_received(c)) {
        fail = "Incomplete download"; goto out;
    }
    eh_set_install_state("verifying");
    if (!ota_open || esp_ota_write_with_offset(ota, header, CHUNK, 0) != ESP_OK) { fail = "Invalid image header"; goto out; }
    ota_open = false;
    if (esp_ota_end(ota) != ESP_OK) { fail = "ESP image validation failed"; goto out; }
    cJSON *result = receipt(in);
    if (!result || !cJSON_AddNumberToObject(result, "bytes", written)) {
        cJSON_Delete(result);
        fail = "Cannot build install receipt"; goto out;
    }
    if (!save_result("install.done", result, target->label)) {
        fail = "Cannot persist install receipt"; goto out;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        fail = "Cannot select boot partition"; goto out;
    }

out:
    if (ota_open) esp_ota_abort(ota);
    if (c) esp_http_client_cleanup(c);
    free(buf);
    free(header);
    if (fail) { emit_failed(in, fail); return false; }
    eh_set_install_state("done");
    eh_schedule_reboot(1500);
    return true;
}
static void install_task(void *arg)
{
    inst_t *in = arg;
    eh_pause_mqtt();
    bool rebooting = run_install(in);
    eh_resume_mqtt();
    free(in);
    if (!rebooting) s_running = false;
    vTaskDelete(NULL);
}

static bool start_task(inst_t *in)
{
    s_running = true;
    if (xTaskCreate(install_task, "eh_install", 8192, in, 2, NULL) != pdPASS) {
        s_running = false;
        return false;
    }
    return true;
}

esp_err_t eh_install_op(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    if (s_running) {
        snprintf(err, errlen, "install already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    inst_t *in;
    esp_err_t parsed = parse_args(args, &in, err, errlen);
    if (parsed != ESP_OK) return parsed;
    /* Keep only the identity needed to report an interrupted transfer. */
    cJSON *p = receipt(in);
    char *s = p ? cJSON_PrintUnformatted(p) : NULL;
    cJSON_Delete(p);
    esp_err_t saved = s ? eh_nvs_set_str(EH_KEY_PENDING, s) : ESP_ERR_NO_MEM;
    free(s);
    if (saved != ESP_OK) {
        free(in);
        snprintf(err, errlen, "Cannot persist pending update");
        return saved;
    }
    eh_set_install_state("queued");
    if (!start_task(in)) {
        free(in);
        eh_nvs_erase(EH_KEY_PENDING);
        snprintf(err, errlen, "cannot start install task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void eh_install_reconcile_boot(void)
{
    // Preserve the OTA outcome before the bootloader can hand off to the application.
    char *saved = eh_nvs_get_str_alloc(EH_KEY_PENDING);
    if (!saved) return;
    cJSON *marker = cJSON_Parse(saved);
    free(saved);
    const char *target = cJSON_GetStringValue(cJSON_GetObjectItem(marker, "target"));
    if (target && !strcmp(target, esp_ota_get_running_partition()->label)) {
        cJSON_DeleteItemFromObject(marker, "target");
        char *result = cJSON_PrintUnformatted(marker);
        if (result) {
            ESP_ERROR_CHECK(eh_nvs_set_str(EH_KEY_PENDING, result));
            free(result);
        }
    } else if (cJSON_IsObject(marker) && !cJSON_GetObjectItem(marker, "t")) {
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(marker, "kind"));
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(marker, "transfer_id"));
        cJSON *data = cJSON_CreateObject();
        if (kind && id && cJSON_AddStringToObject(data, "kind", kind) &&
            cJSON_AddStringToObject(data, "transfer_id", id) &&
            cJSON_AddStringToObject(data, "error", "Installation interrupted")) {
            save_result("install.failed", data, NULL);
        } else {
            cJSON_Delete(data);
        }
    }
    cJSON_Delete(marker);
}
