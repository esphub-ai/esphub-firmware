// SPDX-FileCopyrightText: 2024 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#include "eh_agent.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <stdio.h>
#include <string.h>

#define NUS_UUID(part) BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                                       0x93, 0xf3, 0xa3, 0xb5, part, 0x00, 0x40, 0x6e)
static const ble_uuid128_t service_uuid = NUS_UUID(1);
static const ble_uuid128_t rx_uuid = NUS_UUID(2);
static const ble_uuid128_t tx_uuid = NUS_UUID(3);
static const ble_uuid16_t cccd_uuid = BLE_UUID16_INIT(0x2902);
static const char *const values[] = {"4", "8", "15", "16", "23", "46"};

// Connection state is owned by the NimBLE host task.
static struct ble_npl_callout retry;
static struct ble_npl_callout exchange;
static uint8_t own_addr;
static uint16_t service_start, service_end, rx, tx, tx_end, cccd;
static unsigned index_value;
static bool waiting_for_reply, candidate_found;
static ble_addr_t candidate;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint8_t received[128];
static uint16_t received_size;
static bool truncated;

static int gap_event(struct ble_gap_event *event, void *arg);

static void schedule_scan(void)
{
    ble_npl_callout_reset(&retry, ble_npl_time_ms_to_ticks32(1000));
}

static void fail_connection(const char *step, int error)
{
    printf("%s failed (%d)\n", step, error);
    ble_npl_callout_stop(&exchange);
    if (connection != BLE_HS_CONN_HANDLE_NONE) {
        const int rc = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc != BLE_HS_EALREADY) printf("Disconnect request failed (%d)\n", rc);
    }
}

static void scan(struct ble_npl_event *event)
{
    (void)event;
    candidate_found = false;
    struct ble_gap_disc_params params = {.passive = 0, .filter_duplicates = 1};
    puts("Scanning for ble-uart");
    const int rc = ble_gap_disc(own_addr, 2000, &params, gap_event, NULL);
    if (rc) {
        printf("Scan start failed (%d)\n", rc);
        schedule_scan();
    }
}

static void send_number(struct ble_npl_event *event)
{
    (void)event;
    if (waiting_for_reply) {
        fputs("rx:", stdout);
        for (uint16_t i = 0; i < received_size; ++i) {
            unsigned byte = received[i];
            if (byte >= 0x20 && byte <= 0x7e && byte != '\\') putchar(byte);
            else printf("\\x%02X", byte);
        }
        puts(truncated ? "..." : "");
        received_size = 0;
        truncated = false;
    }
    const char *value = values[index_value];
    // UiFlow2 gattc_write's default mode is write without response.
    const int rc = ble_gattc_write_no_rsp_flat(connection, rx, value, strlen(value));
    if (rc) {
        fail_connection("NUS write", rc);
        return;
    }
    printf("tx:%s\n", value);
    index_value = (index_value + 1) % (sizeof(values) / sizeof(values[0]));
    waiting_for_reply = true;
    ble_npl_callout_reset(&exchange, ble_npl_time_ms_to_ticks32(1000));
}

static int subscribed(uint16_t handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg)
{
    if (error->status) fail_connection("NUS CCCD subscription", error->status);
    else {
        puts("Connected");
        send_number(NULL);
    }
    return 0;
}

static int descriptor(uint16_t handle, const struct ble_gatt_error *error,
                      uint16_t value_handle, const struct ble_gatt_dsc *desc, void *arg)
{
    if (!error->status) {
        if (!ble_uuid_cmp(&desc->uuid.u, &cccd_uuid.u)) cccd = desc->handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !cccd) {
        fail_connection("NUS TX CCCD discovery", error->status);
        return 0;
    }
    const uint8_t enable_notify[] = {1, 0};
    const int rc = ble_gattc_write_flat(handle, cccd, enable_notify, sizeof(enable_notify),
                                       subscribed, NULL);
    if (rc) fail_connection("NUS CCCD write", rc);
    return 0;
}

static int characteristic(uint16_t handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg)
{
    if (!error->status) {
        if (tx && tx_end == service_end) tx_end = chr->def_handle - 1;
        if (!ble_uuid_cmp(&chr->uuid.u, &rx_uuid.u)) rx = chr->val_handle;
        if (!ble_uuid_cmp(&chr->uuid.u, &tx_uuid.u) &&
                (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            tx = chr->val_handle;
            tx_end = service_end;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !rx || !tx || tx_end <= tx) {
        fail_connection("NUS characteristic discovery", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_all_dscs(handle, tx, tx_end, descriptor, NULL);
    if (rc) fail_connection("NUS descriptor request", rc);
    return 0;
}

static int service(uint16_t handle, const struct ble_gatt_error *error,
                   const struct ble_gatt_svc *svc, void *arg)
{
    if (!error->status) {
        service_start = svc->start_handle;
        service_end = svc->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !service_start) {
        fail_connection("NUS service discovery", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(handle, service_start, service_end, characteristic, NULL);
    if (rc) fail_connection("NUS characteristic request", rc);
    return 0;
}

static void connect_peer(void)
{
    printf("Found ble-uart, connecting\n");
    const int rc = ble_gap_connect(own_addr, &candidate, 5000, NULL, gap_event, NULL);
    if (rc) {
        printf("Connect start failed (%d)\n", rc);
        schedule_scan();
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        const struct ble_gap_disc_desc *adv = &event->disc;
        if ((adv->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
             adv->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) ||
            ble_hs_adv_parse_fields(&fields, adv->data, adv->length_data) ||
            !fields.name_is_complete || fields.name_len != 8 ||
            memcmp(fields.name, "ble-uart", 8)) break;
        candidate = adv->addr;
        candidate_found = true;
        // NimBLE cancellation suppresses the discovery-complete callback.
        if (!ble_gap_disc_cancel()) connect_peer();
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (candidate_found) connect_peer();
        else {
            puts("Scan timed out");
            schedule_scan();
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) {
            printf("Connect failed (%d)\n", event->connect.status);
            schedule_scan();
        } else {
            connection = event->connect.conn_handle;
            const int rc = ble_gattc_disc_svc_by_uuid(connection, &service_uuid.u, service, NULL);
            if (rc) fail_connection("NUS service request", rc);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("Disconnected (%d)\n", event->disconnect.reason);
        ble_npl_callout_stop(&exchange);
        connection = BLE_HS_CONN_HANDLE_NONE;
        service_start = service_end = rx = tx = tx_end = cccd = 0;
        received_size = index_value = 0;
        truncated = waiting_for_reply = false;
        schedule_scan();
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.conn_handle == connection &&
            event->notify_rx.attr_handle == tx && !event->notify_rx.indication) {
            const uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
            const uint16_t available = sizeof(received) - received_size;
            const uint16_t count = length < available ? length : available;
            if (!os_mbuf_copydata(event->notify_rx.om, 0, count, received + received_size)) {
                received_size += count;
                truncated |= count != length;
            }
        }
        break;
    }
    return 0;
}

static void synced(void)
{
    const int rc = ble_hs_id_infer_auto(0, &own_addr);
    if (rc) printf("BLE address unavailable (%d)\n", rc);
    else scan(NULL);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    puts("AtomS3 BLE UART Client");
    const esp_err_t status = nimble_port_init();
    if (status != ESP_OK) {
        printf("BLE initialization failed: %s\n", esp_err_to_name(status));
        return;
    }
    ble_npl_callout_init(&retry, nimble_port_get_dflt_eventq(), scan, NULL);
    ble_npl_callout_init(&exchange, nimble_port_get_dflt_eventq(), send_number, NULL);
    ble_hs_cfg.sync_cb = synced;
    nimble_port_freertos_init(host_task);
}
