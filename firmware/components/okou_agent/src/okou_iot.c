/* SPDX-License-Identifier: MIT */
#include "okou_iot.h"
#include "esp_crt_bundle.h"
#include "esp_idf_version.h"
#include "esp_random.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include <inttypes.h>
#include "mbedtls/x509_csr.h"
#include "mqtt_client.h"
#include "okou_priv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_mqtt_client_handle_t mqtt;
static cJSON *active_identity, *active_certificate;
static unsigned events;
static char topic[64], uplink[64], boot[33];
static uint32_t next_mid;
static QueueHandle_t acks, controls;
static char *incoming;
static size_t received, expected;

cJSON *okou_iot_control(void)
{
    cJSON *frame = NULL;
    if (controls) xQueueReceive(controls, &frame, 0);
    return frame;
}
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static void signal_event(unsigned mask)
{
    portENTER_CRITICAL(&event_lock);
    events |= mask;
    portEXIT_CRITICAL(&event_lock);
    okou_agent_notify();
}
unsigned okou_iot_events(void)
{
    portENTER_CRITICAL(&event_lock);
    unsigned mask = events;
    events = 0;
    portEXIT_CRITICAL(&event_lock);
    return mask;
}
typedef struct {
    char *data;
    size_t len, limit;
    bool overflow;
} response_t;
static esp_err_t receive(esp_http_client_event_t *e)
{
    response_t *r = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        if ((size_t)e->data_len > r->limit - r->len) {
            r->overflow = true;
            return ESP_FAIL;
        }
        memcpy(r->data + r->len, e->data, e->data_len);
        r->len += e->data_len;
    }
    return ESP_OK;
}
static cJSON *enroll(cJSON *body)
{
    char base[160], url[224];
    if (!body || okou_hub_http_base(base, sizeof(base)) != ESP_OK)
        return NULL;
    snprintf(url, sizeof(url), "%s/api/device/iot/enroll", base);
    response_t r = {.limit = 4096};
    r.data = okou_malloc_large(r.limit + 1);
    char *payload = cJSON_PrintUnformatted(body);
    cJSON *answer = NULL;
    if (r.data && payload) {
        esp_http_client_config_t cfg = {.url = url,
                                        .method = HTTP_METHOD_POST,
                                        .crt_bundle_attach = esp_crt_bundle_attach,
                                        .disable_auto_redirect = true,
                                        .timeout_ms = 10000,
                                        .event_handler = receive,
                                        .user_data = &r};
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client) {
            okou_http_apply_auth(client);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, payload, strlen(payload));
            if (esp_http_client_perform(client) == ESP_OK) {
                int status = esp_http_client_get_status_code(client);
                r.data[r.len] = 0;
                if (status == 200 && !r.overflow)
                    answer = cJSON_Parse(r.data);
            }
            esp_http_client_cleanup(client);
        }
    }
    free(payload);
    free(r.data);
    return answer;
}
#if ESP_IDF_VERSION_MAJOR < 6
static int hardware_random(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}
#endif

static int generate_key(mbedtls_pk_context *key)
{
#if ESP_IDF_VERSION_MAJOR >= 6
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS)
        return status;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    status = psa_generate_key(&attributes, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS)
        return status;
    int result = mbedtls_pk_copy_from_psa(key_id, key);
    psa_destroy_key(key_id);
    return result;
#else
    int result = mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (result)
        return result;
    return mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(*key), hardware_random, NULL);
#endif
}

static int write_csr(mbedtls_x509write_csr *csr, unsigned char *pem, size_t size)
{
#if ESP_IDF_VERSION_MAJOR >= 6
    return mbedtls_x509write_csr_pem(csr, pem, size);
#else
    return mbedtls_x509write_csr_pem(csr, pem, size, hardware_random, NULL);
#endif
}

/* Save key and original CSR together so a retry uses the identical enrollment. */
static cJSON *identity(void)
{
    char *saved = okou_nvs_get_str_alloc("mqtt_identity");
    if (saved) {
        cJSON *id = cJSON_Parse(saved);
        free(saved);
        return id;
    }
    mbedtls_pk_context key;
    mbedtls_x509write_csr csr;
    mbedtls_pk_init(&key);
    mbedtls_x509write_csr_init(&csr);
    unsigned char *pem = calloc(1, 1024);
    cJSON *id = NULL;
    if (!pem || generate_key(&key) ||
        mbedtls_pk_write_key_pem(&key, pem, 1024))
        goto done;
    id = cJSON_CreateObject();
    if (!id || !cJSON_AddStringToObject(id, "key", (char *)pem))
        goto failed;
    mbedtls_x509write_csr_set_key(&csr, &key);
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    if (mbedtls_x509write_csr_set_subject_name(&csr, "CN=ESPHub device") ||
        write_csr(&csr, pem, 1024) ||
        !cJSON_AddStringToObject(id, "csr", (char *)pem))
        goto failed;
    char *encoded = cJSON_PrintUnformatted(id);
    bool stored = encoded && okou_nvs_set_str("mqtt_identity", encoded) == ESP_OK;
    free(encoded);
    if (stored)
        goto done;
failed:
    cJSON_Delete(id);
    id = NULL;
done:
    free(pem);
    mbedtls_x509write_csr_free(&csr);
    mbedtls_pk_free(&key);
    return id;
}
static void mqtt_event(void *ctx, esp_event_base_t base, int32_t event, void *arg)
{
    esp_mqtt_event_handle_t e = arg;
    switch (event) {
    case MQTT_EVENT_CONNECTED:
        if (esp_mqtt_client_subscribe(e->client, topic, 1) < 0)
            signal_event(IOT_OFFLINE);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        signal_event((e->data_len == 1 && e->data && ((unsigned char)e->data[0]) <= 1) ? IOT_SUBSCRIBED
                                                                                       : IOT_OFFLINE);
        break;
    case MQTT_EVENT_DATA: {
        if (e->current_data_offset == 0) {
            free(incoming);
            incoming = NULL;
            received = 0;
            expected = e->total_data_len;
            if (expected && expected <= 16384)
                incoming = okou_malloc_large(expected);
        }
        if (!incoming || (size_t)e->current_data_offset != received ||
            e->data_len <= 0 || (size_t)e->data_len > expected - received)
            break;
        memcpy(incoming + received, e->data, e->data_len);
        received += e->data_len;
        if (received < expected) break;
        cJSON *m = cJSON_ParseWithLength(incoming, expected);
        free(incoming);
        incoming = NULL;
        const char *pid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "boot_id"));
        const cJSON *mid = cJSON_GetObjectItemCaseSensitive(m, "mid");
        if (pid && !strcmp(pid, boot) && cJSON_IsNumber(mid) && mid->valuedouble >= 1 &&
            mid->valuedouble <= UINT32_MAX) {
            uint32_t ack = (uint32_t)mid->valuedouble;
            xQueueSend(acks, &ack, 0);
        }
        if (pid && !strcmp(pid, boot) && cJSON_GetObjectItemCaseSensitive(m, "command")) {
            if (xQueueSend(controls, &m, 0) == pdTRUE) {
                okou_agent_notify();
                break;
            }
            // A fresh hello requests the outstanding command again.
            signal_event(IOT_OFFLINE);
        }
        cJSON_Delete(m);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        signal_event(IOT_OFFLINE);
        break;
    default:
        break;
    }
}
void okou_iot_stop(void)
{
    if (mqtt) {
        esp_mqtt_client_destroy(mqtt);
        mqtt = NULL;
    }
    free(incoming);
    incoming = NULL;
    cJSON *frame;
    while ((frame = okou_iot_control())) cJSON_Delete(frame);
    cJSON_Delete(active_identity);
    active_identity = NULL;
    cJSON_Delete(active_certificate);
    active_certificate = NULL;
    okou_iot_events();
}
bool okou_iot_publish(cJSON *frame, bool confirm)
{
    if (!mqtt || !acks || !frame)
        return false;
    uint32_t mid = ++next_mid;
    if (!cJSON_AddStringToObject(frame, "boot_id", boot))
        return false;
    if (!cJSON_AddNumberToObject(frame, "mid", mid) || (confirm && !cJSON_AddTrueToObject(frame, "confirm")))
        return false;
    cJSON *envelope = cJSON_CreateObject();
    if (!envelope || !cJSON_AddItemReferenceToObject(envelope, "frame", frame)) {
        cJSON_Delete(envelope);
        return false;
    }
    char *payload = cJSON_PrintUnformatted(envelope);
    cJSON_Delete(envelope);
    if (!payload)
        return false;
    bool done = false;
    for (int attempt = 0; attempt < (confirm ? 2 : 1) && !done; ++attempt) {
        if (esp_mqtt_client_publish(mqtt, uplink, payload, 0, 1, 0) < 0)
            break;
        if (!confirm) {
            done = true;
            break;
        }
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(6000);
        for (;;) {
            int32_t remaining = (int32_t)(deadline - xTaskGetTickCount());
            uint32_t ack;
            if (remaining <= 0 || !xQueueReceive(acks, &ack, (TickType_t)remaining))
                break;
            if (ack == mid) {
                done = true;
                break;
            }
        }
    }
    free(payload);
    return done;
}

bool okou_iot_publish_log(const uint8_t *bytes, size_t len, uint32_t dropped,
                          char wire[OKOU_LOG_WIRE_BYTES])
{
    if (!mqtt || !len || len > OKOU_LOG_CHUNK_BYTES) return false;
    int header = snprintf(wire, OKOU_LOG_WIRE_BYTES,
                          "{\"frame\":{\"t\":\"log\",\"boot_id\":\"%s\",\"mid\":%" PRIu32
                          ",\"dropped\":%" PRIu32 ",\"bytes\":\"", boot, ++next_mid, dropped);
    if (header < 0 || header >= OKOU_LOG_WIRE_BYTES - 4) return false;
    size_t encoded;
    if (mbedtls_base64_encode((uint8_t *)wire + header, OKOU_LOG_WIRE_BYTES - header - 3,
                              &encoded, bytes, len) != 0) return false;
    memcpy(wire + header + encoded, "\"}}", 4);
    // Synchronous QoS 0 uses the client's existing TX buffer. QoS 1 or
    // enqueue(store=true) would also allocate an outbox copy per message.
    return esp_mqtt_client_publish(mqtt, uplink, wire, header + (int)encoded + 3, 0, 0) >= 0;
}

bool okou_iot_start(const char *boot_id, const char *hub, const char *token)
{
    cJSON *id = identity(), *req = cJSON_CreateObject(), *reply = NULL;
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(id, "key"));
    const char *csr = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(id, "csr"));
    bool started = false;
    if (!acks)
        acks = xQueueCreate(8, sizeof(uint32_t));
    if (!controls)
        controls = xQueueCreate(4, sizeof(cJSON *));
    if (!acks || !controls)
        goto done;
    xQueueReset(acks);
    strlcpy(boot, boot_id, sizeof(boot));
    if (!key || !csr || !cJSON_AddStringToObject(req, "csr", csr))
        goto done;
    // Bind the cached grant to the exact provisioning settings. USB reissue
    // or a different hub must never connect using the previous registration.
    char binding[65];
    unsigned char hash[32];
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    size_t hash_len = 0;
    int rc = psa_crypto_init() ||
             psa_hash_setup(&sha, PSA_ALG_SHA_256) ||
             psa_hash_update(&sha, (const unsigned char *)hub, strlen(hub) + 1) ||
             psa_hash_update(&sha, (const unsigned char *)token, strlen(token)) ||
             psa_hash_finish(&sha, hash, sizeof(hash), &hash_len);
    psa_hash_abort(&sha);
    if (rc || hash_len != sizeof(hash)) goto done;
    okou_hex(hash, sizeof(hash), binding);
    char *saved = okou_nvs_get_str_alloc("mqtt_grant");
    if (saved) {
        reply = cJSON_Parse(saved);
        free(saved);
    }
    const char *bound = cJSON_GetStringValue(cJSON_GetObjectItem(reply, "binding"));
    if (!bound || strcmp(bound, binding)) {
        cJSON_Delete(reply);
        reply = enroll(req);
        if (!reply || !cJSON_AddStringToObject(reply, "binding", binding)) goto done;
        saved = cJSON_PrintUnformatted(reply);
        bool stored = saved && okou_nvs_set_str("mqtt_grant", saved) == ESP_OK;
        free(saved);
        if (!stored) goto done;
    }
    const char *device = cJSON_GetStringValue(cJSON_GetObjectItem(reply, "device_id"));
    const char *host = cJSON_GetStringValue(cJSON_GetObjectItem(reply, "endpoint"));
    const char *cert = cJSON_GetStringValue(cJSON_GetObjectItem(reply, "certificate"));
    if (!device || strlen(device) != 36 || !host || !*host || !cert || !*cert) {
        okou_nvs_erase("mqtt_grant");
        goto done;
    }
    char client_id[48];
    snprintf(client_id, sizeof(client_id), "esphub-%s", device);
    snprintf(topic, sizeof(topic), "devices/%s/wake", device);
    snprintf(uplink, sizeof(uplink), "devices/%s/up", device);
    esp_mqtt_client_config_t config = {
        .broker.address.hostname = host,
        .broker.address.port = 8883,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = client_id,
        .credentials.authentication.certificate = cert,
        .credentials.authentication.key = key,
        .session.keepalive = 300,
        .network.disable_auto_reconnect = true,
    };
    mqtt = esp_mqtt_client_init(&config);
    if (mqtt && esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL) == ESP_OK &&
        esp_mqtt_client_start(mqtt) == ESP_OK)
        started = true;
    if (!started)
        okou_iot_stop();
    else {
        active_identity = id;
        active_certificate = reply;
        id = reply = NULL;
    }
done:
    cJSON_Delete(req);
    cJSON_Delete(reply);
    cJSON_Delete(id);
    return started;
}
