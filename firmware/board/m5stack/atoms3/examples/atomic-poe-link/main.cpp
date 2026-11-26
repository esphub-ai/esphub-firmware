// Adapted from the MIT-licensed M5AtomS3 AtomicBase / AtomicPoE / LinkStatus example.
#include "eh_agent.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr auto ethernetBus = SPI2_HOST;  // The AtomS3 LCD uses SPI3.
constexpr uint16_t versionRegister = 0x0039;
constexpr uint16_t phyConfigRegister = 0x002e;
constexpr uint8_t w5500Version = 0x04;
constexpr uint8_t phyLink = 0x01;

bool readRegister(spi_device_handle_t ethernet, uint16_t address, uint8_t& value)
{
    spi_transaction_t transaction{};
    transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    transaction.length = 32;
    transaction.tx_data[0] = address >> 8;
    transaction.tx_data[1] = address & 0xff;
    // Control byte 0 selects a common-register read in variable-length mode.
    if (spi_device_polling_transmit(ethernet, &transaction) != ESP_OK) {
        return false;
    }
    value = transaction.rx_data[3];
    return true;
}

enum class Link { unknown, on, off };

Link linkStatus(spi_device_handle_t ethernet)
{
    uint8_t version;
    uint8_t phy;
    if (!readRegister(ethernet, versionRegister, version) || version != w5500Version ||
        !readRegister(ethernet, phyConfigRegister, phy)) {
        return Link::unknown;
    }
    return phy & phyLink ? Link::on : Link::off;
}

void showLabel(const char* label, int y)
{
    auto& display = M5.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::Orbitron_Light_24);
    display.drawString(label, display.width() / 2, y);
}
}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(eh_agent_start());
    M5.begin(M5.config());
    showLabel("PoE", 20);
    std::puts("AtomicPoE start SPI2 W5500 SCK=5 MISO=7 MOSI=8 CS=6");
    vTaskDelay(pdMS_TO_TICKS(250));

    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_5;
    bus.miso_io_num = GPIO_NUM_7;
    bus.mosi_io_num = GPIO_NUM_8;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    ESP_ERROR_CHECK(spi_bus_initialize(ethernetBus, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t device{};
    device.clock_speed_hz = 4 * 1000 * 1000;
    device.spics_io_num = GPIO_NUM_6;
    device.queue_size = 1;
    spi_device_handle_t ethernet = nullptr;
    ESP_ERROR_CHECK(spi_bus_add_device(ethernetBus, &device, &ethernet));

    auto previous = Link::unknown;
    bool firstSample = true;
    for (;;) {
        const auto link = linkStatus(ethernet);
        const char* label = link == Link::on ? "ON" : link == Link::off ? "OFF" : "Unknown";
        if (firstSample || link != previous) {
            showLabel(label, M5.Display.height() / 2);
            firstSample = false;
            previous = link;
        }
        std::printf("Link status: %s\n", label);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
