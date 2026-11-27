// Shared AtomS3 Atomic PoE Base Ethernet setup.
#pragma once

#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace atomic_poe {
constexpr EventBits_t linkUp = BIT0;
constexpr EventBits_t hasAddress = BIT1;
constexpr EventBits_t ready = linkUp | hasAddress;
struct Network {
    esp_eth_handle_t driver{};
    esp_netif_t* netif{};
    EventGroupHandle_t events{};
};

inline void show(const char* title, const char* detail = "")
{
    auto& lcd = M5.Display;
    lcd.clear();
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setFont(&fonts::Orbitron_Light_24);
    lcd.setTextSize(0.8f);
    if (lcd.textWidth(title) > lcd.width() - 8) lcd.setTextSize(0.65f);
    lcd.setTextDatum(middle_center);
    lcd.drawString(title, lcd.width() / 2, 14);
    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);
    lcd.setTextDatum(top_left);
    lcd.drawString(detail, 4, 30);
}

inline void onLink(void* context, esp_event_base_t, int32_t event, void* data)
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

inline void onAddress(void* context, esp_event_base_t, int32_t event, void* data)
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

inline void startEthernet(Network& network)
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

}  // namespace atomic_poe
