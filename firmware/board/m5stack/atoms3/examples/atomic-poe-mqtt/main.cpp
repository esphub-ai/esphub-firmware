// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicPoE / MQTT example.
#include "eh_agent.h"
#include "../atomic_poe.hpp"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/queue.h"
#include <algorithm>
#include <cstring>
#include <net/if.h>

#ifndef ATOMIC_POE_MQTT_BROKER
#define ATOMIC_POE_MQTT_BROKER "mqtt://broker.emqx.io:1883"
#endif

namespace {
using namespace atomic_poe;

constexpr char uplink[] = "LAN_UPLINK";
constexpr char downlink[] = "LAN_DOWNLINK";
constexpr int64_t publishIntervalUs = 3000000;
constexpr size_t displayColumns = 20;
constexpr size_t displayRows = 6;
constexpr size_t payloadLimit = displayColumns * displayRows;

enum class EventKind { connected, failed, message };

struct Event {
    EventKind kind;
    char payload[payloadLimit + 1];
};

struct Inbox {
    QueueHandle_t state;
    QueueHandle_t messages;
    Event message{};
    bool matchesTopic = false;
};

void onMqtt(void* context, esp_event_base_t, int32_t id, void* data)
{
    auto& inbox = *static_cast<Inbox*>(context);
    const auto& event = *static_cast<esp_mqtt_event_t*>(data);

    if (id == MQTT_EVENT_CONNECTED || id == MQTT_EVENT_DISCONNECTED) {
        inbox.matchesTopic = false;
        Event notification{id == MQTT_EVENT_CONNECTED ? EventKind::connected : EventKind::failed, {}};
        xQueueOverwrite(inbox.state, &notification);
        return;
    }
    if (id != MQTT_EVENT_DATA) return;

    if (event.current_data_offset == 0) {
        inbox.message = {EventKind::message, {}};
        inbox.matchesTopic = event.topic_len == sizeof(downlink) - 1 &&
                             std::memcmp(event.topic, downlink, sizeof(downlink) - 1) == 0;
    }
    if (!inbox.matchesTopic) return;

    const size_t offset = event.current_data_offset;
    const bool truncated = size_t(event.total_data_len) > payloadLimit;
    const size_t limit = truncated ? payloadLimit - 3 : payloadLimit;
    const size_t end = std::min(limit, offset + event.data_len);
    for (size_t i = offset; i < end; ++i) {
        auto byte = static_cast<unsigned char>(event.data[i - offset]);
        inbox.message.payload[i] = byte >= 32 && byte <= 126 ? char(byte) : ' ';
    }
    if (event.current_data_offset + event.data_len == event.total_data_len) {
        if (truncated) std::memcpy(inbox.message.payload + limit, "...", 3);
        xQueueSend(inbox.messages, &inbox.message, 0);
    }
}

void showMessage(const Event& event)
{
    std::printf("Message arrived [%s] %s\n", downlink, event.payload);
    show("Received", downlink);
    const size_t length = std::strlen(event.payload);
    for (size_t offset = 0; offset < length; offset += displayColumns) {
        char line[displayColumns + 1]{};
        std::memcpy(line, event.payload + offset, std::min(displayColumns, length - offset));
        M5.Display.drawString(line, 4, 44 + offset / displayColumns * 12);
    }
}

void runMqtt(Network& network)
{
    struct ifreq interface{};
    ESP_ERROR_CHECK(esp_netif_get_netif_impl_name(network.netif, interface.ifr_name));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_ETH));
    char clientId[32];
    std::snprintf(clientId, sizeof(clientId), "atoms3-poe-%02x%02x%02x%02x%02x%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Inbox inbox{xQueueCreate(1, sizeof(Event)), xQueueCreate(8, sizeof(Event))};
    ESP_ERROR_CHECK(inbox.state && inbox.messages ? ESP_OK : ESP_ERR_NO_MEM);
    esp_mqtt_client_config_t config{};
    config.broker.address.uri = ATOMIC_POE_MQTT_BROKER;
    config.credentials.client_id = clientId;
    config.network.if_name = &interface;
    config.network.timeout_ms = 3000;
    config.network.reconnect_timeout_ms = 5000;
    config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    auto client = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(client ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onMqtt, &inbox));

    bool started = false;
    bool connected = false;
    int64_t nextPublish = 0;
    for (;;) {
        const auto bits = xEventGroupGetBits(network.events);
        if ((bits & ready) != ready) {
            if (started) {
                ESP_ERROR_CHECK(esp_mqtt_client_stop(client));
                xQueueReset(inbox.state);
                xQueueReset(inbox.messages);
                started = connected = false;
            }
            if (bits & linkUp) {
                show("DHCP", "Waiting for address");
                std::puts("Waiting for DHCP; lwIP retries acquisition");
            } else {
                show("W5500", "Cable disconnected");
                std::puts("Ethernet cable is not connected.");
            }
            xEventGroupWaitBits(network.events, ready, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        if (!started) {
            show("MQTT", "Connecting...");
            ESP_ERROR_CHECK(esp_mqtt_client_start(client));
            started = true;
        }
        Event event;
        if (xQueueReceive(inbox.state, &event, 0) == pdTRUE) {
            switch (event.kind) {
            case EventKind::connected:
                connected = true;
                show("Connected!");
                std::puts("MQTT Connected!");
                if (esp_mqtt_client_subscribe(client, downlink, 0) >= 0) {
                    std::printf("Subscribe %s\n", downlink);
                }
                if (esp_mqtt_client_publish(client, uplink, "hello world", 0, 0, 0) >= 0) {
                    std::printf("Initial publish [%s] hello world\n", uplink);
                }
                nextPublish = esp_timer_get_time() + publishIntervalUs;
                break;
            case EventKind::failed:
                connected = false;
                show("Failed!", "Retry in 5 seconds");
                std::puts("MQTT Failed! Retry in 5 seconds");
                break;
            case EventKind::message:
                break;
            }
        }
        if (xQueueReceive(inbox.messages, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            showMessage(event);
        }
        const auto now = esp_timer_get_time();
        if (connected && now >= nextPublish) {
            nextPublish = now + publishIntervalUs;
            char payload[40];
            std::snprintf(payload, sizeof(payload), "hello world: %lld", static_cast<long long>(now / 1000));
            if (esp_mqtt_client_publish(client, uplink, payload, 0, 0, 0) >= 0) {
                show("Publish!", uplink);
                M5.Display.drawString(payload, 4, 44);
                std::printf("Publish! [%s] %s\n", uplink, payload);
            }
        }
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Init...");
    std::puts("AtomicPoE MQTT Init");
    vTaskDelay(pdMS_TO_TICKS(300));
    Network network;
    network.events = xEventGroupCreate();
    ESP_ERROR_CHECK(network.events ? ESP_OK : ESP_ERR_NO_MEM);
    startEthernet(network);
    runMqtt(network);
}
