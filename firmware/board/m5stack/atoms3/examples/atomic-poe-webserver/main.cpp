// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicPoE / WebServer example.
#include "eh_agent.h"
#include "../atomic_poe.hpp"
#include "esp_timer.h"
#include <cerrno>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using namespace atomic_poe;

constexpr char response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "Refresh: 5\r\n\r\n"
    "<!DOCTYPE HTML>\r\n<html>\r\n<h2>Hello M5Stack LAN Module!</h2>\r\n</html>\r\n";

uint32_t address(const Network& network)
{
    if ((xEventGroupGetBits(network.events) & ready) != ready) return 0;
    esp_netif_ip_info_t ip{};
    return esp_netif_get_ip_info(network.netif, &ip) == ESP_OK ? ip.ip.addr : 0;
}

int listenOn(esp_netif_t* netif, uint32_t ip)
{
    struct ifreq interface{};
    if (esp_netif_get_netif_impl_name(netif, interface.ifr_name) != ESP_OK) return -1;
    const auto server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0) return -1;
    const int reuse = 1;
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(80);
    endpoint.sin_addr.s_addr = ip;
    if (setsockopt(server, SOL_SOCKET, SO_BINDTODEVICE, &interface, sizeof(interface)) ||
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) ||
        bind(server, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) ||
        listen(server, 4)) {
        std::printf("W5500 port 80 setup failed: errno=%d\n", errno);
        close(server);
        return -1;
    }
    return server;
}

bool serve(int client, const Network& network, uint32_t ip)
{
    const timeval timeout{0, 250000};
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) return false;

    uint32_t ending = 0;
    size_t bytes = 0;
    const int64_t deadline = esp_timer_get_time() + 2000000;
    while (bytes < 2048 && address(network) == ip && esp_timer_get_time() < deadline) {
        char header[128];
        const auto received = recv(client, header, sizeof(header), 0);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (received <= 0) return false;
        for (int i = 0; i < received && bytes < 2048; ++i, ++bytes) {
            ending = (ending << 8) | static_cast<unsigned char>(header[i]);
            if (ending != 0x0d0a0d0a && (ending & 0xffff) != 0x0a0a) continue;

            size_t sent = 0;
            while (sent < sizeof(response) - 1) {
                const auto count = send(client, response + sent, sizeof(response) - 1 - sent, 0);
                if (count <= 0) return false;
                sent += count;
            }
            return true;
        }
    }
    return false;
}

void runServer(Network& network)
{
    for (;;) {
        const auto ip = address(network);
        if (!ip) {
            if (xEventGroupGetBits(network.events) & linkUp) {
                show("DHCP", "Waiting for address");
                std::puts("Waiting for DHCP; lwIP retries acquisition");
            } else {
                show("W5500", "Cable disconnected");
                std::puts("Ethernet cable is not connected.");
            }
            xEventGroupWaitBits(network.events, ready, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        const auto server = listenOn(network.netif, ip);
        if (server < 0) {
            show("WebServer", "Retrying port 80");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        esp_ip4_addr_t ipAddress{ip};
        char label[16];
        std::snprintf(label, sizeof(label), IPSTR, IP2STR(&ipAddress));
        show("WebServer", label);
        std::printf("server is at %s\n", label);

        while (address(network) == ip) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(server, &readable);
            timeval timeout{0, 250000};
            const auto selected = select(server + 1, &readable, nullptr, nullptr, &timeout);
            if (selected < 0 && errno != EINTR) break;
            if (selected <= 0 || address(network) != ip) continue;
            const auto client = accept(server, nullptr, nullptr);
            if (client < 0) continue;
            std::puts("new client");
            const bool replied = serve(client, network, ip);
            close(client);
            if (replied) {
                M5.Display.drawString("HTTP 200 OK", 4, 44);
                std::puts("HTTP 200 OK");
            }
            std::puts("client disconnected");
        }
        close(server);
        std::puts("W5500 HTTP listener closed");
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    show("Init...");
    std::puts("AtomicPoE WebServer Init");
    vTaskDelay(pdMS_TO_TICKS(300));
    Network network;
    network.events = xEventGroupCreate();
    ESP_ERROR_CHECK(network.events ? ESP_OK : ESP_ERR_NO_MEM);
    startEthernet(network);
    runServer(network);
}
