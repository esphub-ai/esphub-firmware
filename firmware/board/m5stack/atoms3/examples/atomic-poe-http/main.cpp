// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicPoE / HTTP example.
#include "eh_agent.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <net/if.h>

#ifndef ATOMIC_POE_HTTP_ORIGIN
#define ATOMIC_POE_HTTP_ORIGIN "http://httpbin.org"
#endif

namespace {
constexpr EventBits_t linkUp = BIT0;
constexpr EventBits_t hasAddress = BIT1;
constexpr EventBits_t ready = linkUp | hasAddress;
constexpr size_t responseLimit = 2048;
constexpr size_t columns = 20;
constexpr size_t rows = 7;

struct Network {
    esp_eth_handle_t driver{};
    esp_netif_t* netif{};
    EventGroupHandle_t events{};
};

struct Response {
    char body[responseLimit + 1]{};
    size_t length{};
    bool truncated{};
};

void show(const char* title, const char* detail = "")
{
    auto& lcd = M5.Display;
    lcd.clear();
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setFont(&fonts::Orbitron_Light_24);
    lcd.setTextSize(0.8f);
    lcd.setTextDatum(middle_center);
    lcd.drawString(title, lcd.width() / 2, 14);
    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextDatum(top_left);
    lcd.drawString(detail, 4, 30);
}

void onLink(void* context, esp_event_base_t, int32_t event, void* data)
{
    auto& network = *static_cast<Network*>(context);
    if (*static_cast<esp_eth_handle_t*>(data) != network.driver) return;
    if (event == ETHERNET_EVENT_CONNECTED) {
        xEventGroupSetBits(network.events, linkUp);
        std::puts("W5500 link ON");
    } else if (event == ETHERNET_EVENT_DISCONNECTED || event == ETHERNET_EVENT_STOP) {
        xEventGroupClearBits(network.events, ready);
        std::puts("W5500 link OFF");
    }
}

void onAddress(void* context, esp_event_base_t, int32_t event, void* data)
{
    auto& network = *static_cast<Network*>(context);
    const auto& address = *static_cast<ip_event_got_ip_t*>(data);
    if (address.esp_netif != network.netif) return;
    if (event == IP_EVENT_ETH_LOST_IP) {
        xEventGroupClearBits(network.events, hasAddress);
        return;
    }
    std::printf("W5500 DHCP " IPSTR "\n", IP2STR(&address.ip_info.ip));
    xEventGroupSetBits(network.events, hasAddress);
}

void startEthernet(Network& network)
{
    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_5;
    bus.miso_io_num = GPIO_NUM_7;
    bus.mosi_io_num = GPIO_NUM_8;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 1600;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t spi{};
    spi.clock_speed_hz = 4 * 1000 * 1000;
    spi.spics_io_num = GPIO_NUM_6;
    spi.queue_size = 1;
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &spi);
    w5500.int_gpio_num = -1;
    w5500.poll_period_ms = 30;
    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    phyConfig.reset_gpio_num = -1;
    phyConfig.phy_addr = 0;

    for (;;) {
        auto* mac = esp_eth_mac_new_w5500(&w5500, &macConfig);
        auto* phy = esp_eth_phy_new_w5500(&phyConfig);
        ESP_ERROR_CHECK(mac && phy ? ESP_OK : ESP_ERR_NO_MEM);
        esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
        if (esp_eth_driver_install(&config, &network.driver) == ESP_OK) break;
        phy->del(phy);
        mac->del(mac);
        show("W5500", "Ethernet not found");
        std::puts("W5500 not found; retrying");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(network.driver, ETH_CMD_S_MAC_ADDR, mac));
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    // The SDK may already have an Ethernet uplink (for example, in QEMU).
    if (esp_netif_get_handle_from_ifkey(base.if_key)) base.if_key = "ATOMIC_POE";
    base.route_prio = 110;
    esp_netif_config_t config = ESP_NETIF_DEFAULT_ETH();
    config.base = &base;
    network.netif = esp_netif_new(&config);
    auto glue = esp_eth_new_netif_glue(network.driver);
    ESP_ERROR_CHECK(network.netif && glue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_attach(network.netif, glue));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onLink, &network));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onAddress, &network));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, onAddress, &network));
    ESP_ERROR_CHECK(esp_eth_start(network.driver));
    std::puts("W5500 initialized on SPI2 SCK=5 MISO=7 MOSI=8 CS=6");
}

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
        char detail[24];
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
