// https://docs.m5stack.com/en/arduino/papercolor/microsd
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr char mountPoint[] = "/sd";
constexpr const char* pictures[] = {"/sd/picture_01.png", "/sd/picture_02.png"};
constexpr int64_t switchInterval = 60 * 1000000;

void showError(const char* message)
{
    auto& display = M5.Display;
    display.startWrite();
    display.fillScreen(WHITE);
    display.setFont(&fonts::Font4);
    display.setTextDatum(middle_center);
    display.setTextColor(RED);
    display.drawString(message, display.width() / 2, display.height() / 2);
    display.endWrite();
    std::puts(message);
}

bool mountCard()
{
    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_15;
    bus.mosi_io_num = GPIO_NUM_13;
    bus.miso_io_num = GPIO_NUM_14;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) {
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 25000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = GPIO_NUM_47;
    esp_vfs_fat_sdmmc_mount_config_t options{};
    options.max_files = 1;
    sdmmc_card_t* card = nullptr;
    const auto status = esp_vfs_fat_sdspi_mount(mountPoint, &host, &slot, &options, &card);
    if (status != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        std::printf("SD mount error: %s\n", esp_err_to_name(status));
        return false;
    }
    std::puts("microSD mounted");
    return true;
}

bool showPicture(M5Canvas& canvas, unsigned index)
{
    canvas.fillSprite(WHITE);
    lgfx::DataWrapperT<FILE> file;
    if (!canvas.drawPngFile(&file, pictures[index])) {
        showError("PNG load failed");
        std::printf("Failed to draw PNG: %s\n", pictures[index]);
        return false;
    }
    canvas.pushSprite(0, 0);
    std::printf("Displayed: %s at %lld us\n", pictures[index], static_cast<long long>(esp_timer_get_time()));
    return true;
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    M5.Display.setRotation(1);

    auto& pmic = M5.Power.M5pm1;
    using PM1 = m5::M5PM1_Class;
    bool pinsOk = pmic.begin() &&
                  pmic.setLDOOutput(true) &&
                  pmic.setGPIOFunction(PM1::gpio0, PM1::gpio) &&
                  pmic.setGPIOMode(PM1::gpio0, PM1::output) &&
                  pmic.setGPIOOutput(PM1::gpio0, true) &&
                  pmic.setGPIOFunction(PM1::gpio4, PM1::gpio) &&
                  pmic.setGPIOMode(PM1::gpio4, PM1::output) &&
                  pmic.setGPIOOutput(PM1::gpio4, true) &&
                  pmic.setGPIOFunction(PM1::gpio3, PM1::gpio) &&
                  pmic.setGPIOMode(PM1::gpio3, PM1::output) &&
                  pmic.setGPIOOutput(PM1::gpio3, true) &&
                  pmic.setGPIOFunction(PM1::gpio1, PM1::gpio) &&
                  pmic.setGPIOMode(PM1::gpio1, PM1::input) &&
                  pmic.setGPIOPull(PM1::gpio1, PM1::pull_up);
    uint8_t inputs = 0;
    bool playing = false;
    M5Canvas canvas(&M5.Display);
    if (!pinsOk || !pmic.getGPIOInputBits(&inputs)) {
        showError("PM1 init failed");
    } else if (inputs & (1 << 1)) {
        showError("Card not inserted");
    } else {
        std::puts("microSD detected (CARD_DEC low)");
        if (!mountCard()) {
            showError("SD init failed");
        } else if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
            showError("Display buffer unavailable");
        } else {
            playing = showPicture(canvas, 0);
        }
    }

    unsigned index = 0;
    int64_t lastSwitch = esp_timer_get_time();
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        if (playing && now - lastSwitch >= switchInterval) {
            lastSwitch = now;
            index ^= 1;
            showPicture(canvas, index);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
