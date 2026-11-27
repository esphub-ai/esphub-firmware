// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicPoE / HTTP example.
#include "eh_agent.h"
#include "esp_http_client.h"
#include "../atomic_poe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <net/if.h>

#ifndef ATOMIC_POE_HTTP_ORIGIN
#define ATOMIC_POE_HTTP_ORIGIN "http://httpbin.org"
#endif

namespace {
using namespace atomic_poe;
constexpr size_t responseLimit = 2048;
constexpr size_t columns = 20;
constexpr size_t rows = 7;

struct Response {
    char body[responseLimit + 1]{};
    size_t length{};
    bool truncated{};
};

esp_err_t receive(esp_http_client_event_t* event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto& response = *static_cast<Response*>(event->user_data);
    const auto received = static_cast<size_t>(event->data_len);
    const auto count = std::min(received, responseLimit - response.length);
    std::memcpy(response.body + response.length, event->data, count);
    response.length += count;
    response.truncated |= received > count;
    return ESP_OK;
}

void displayResponse(const char* method, int status, const Response& response)
{
    char heading[24];
    std::snprintf(heading, sizeof(heading), "%s %d", method, status);
    const auto pages = std::max<size_t>(1, (response.length + columns * rows - 1) / (columns * rows));
    for (size_t page = 0; page < pages; ++page) {
        char detail[40];
        std::snprintf(detail, sizeof(detail), "Response %u/%u%s",
                      unsigned(page + 1), unsigned(pages), response.truncated ? " +" : "");
        show(heading, detail);
        for (size_t row = 0; row < rows; ++row) {
            char line[columns + 1]{};
            size_t start = (page * rows + row) * columns;
            for (size_t col = 0; col < columns && start + col < response.length; ++col) {
                auto byte = static_cast<unsigned char>(response.body[start + col]);
                line[col] = byte >= 32 && byte <= 126 ? char(byte) : ' ';
            }
            M5.Display.drawString(line, 4, 44 + row * 11);
        }
        vTaskDelay(pdMS_TO_TICKS(pages > 1 ? 1500 : 5000));
    }
}

void request(Network& network, bool post)
{
    const char* method = post ? "POST" : "GET";
    const char* url = post ? ATOMIC_POE_HTTP_ORIGIN "/post" : ATOMIC_POE_HTTP_ORIGIN "/get";
    show(method, "Requesting...");
    std::printf("making %s request\n", method);
    struct ifreq interface{};
    ESP_ERROR_CHECK(esp_netif_get_netif_impl_name(network.netif, interface.ifr_name));
    Response response;
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    cfg.timeout_ms = 5000;
    cfg.disable_auto_redirect = true;
    cfg.event_handler = receive;
    cfg.user_data = &response;
    cfg.if_name = &interface;
    auto client = esp_http_client_init(&cfg);
    esp_err_t err = ESP_ERR_NO_MEM;
    int status = 0;
    if (client) {
        if (post) {
            constexpr char form[] = "name=Alice&age=12";
            esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
            esp_http_client_set_post_field(client, form, sizeof(form) - 1);
            err = esp_http_client_perform(client);
        } else {
            err = esp_http_client_perform(client);
        }
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
    }
    if (err != ESP_OK) {
        const char* error = esp_err_to_name(err);
        std::printf("%s failed: %s\n", method, error);
        show(method, error);
        vTaskDelay(pdMS_TO_TICKS(5000));
        return;
    }
    std::printf("Status code: %d\nResponse: ", status);
    std::fwrite(response.body, 1, response.length, stdout);
    std::printf("%s\n", response.truncated ? " [truncated]" : "");
    displayResponse(method, status, response);
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Init...");
    std::puts("AtomicPoE HTTP Init");
    vTaskDelay(pdMS_TO_TICKS(300));
    Network network;
    network.events = xEventGroupCreate();
    ESP_ERROR_CHECK(network.events ? ESP_OK : ESP_ERR_NO_MEM);
    startEthernet(network);
    bool post = false;
    for (;;) {
        auto bits = xEventGroupGetBits(network.events);
        if ((bits & ready) == ready) {
            request(network, post);
            post = !post;
            continue;
        }
        if (!(bits & linkUp)) {
            show("W5500", "Cable disconnected");
            std::puts("Ethernet cable is not connected.");
        } else {
            show("DHCP", "Waiting for address");
            std::puts("Waiting for DHCP; lwIP retries acquisition");
        }
        xEventGroupWaitBits(network.events, ready, pdFALSE, pdTRUE, pdMS_TO_TICKS(2000));
    }
}
