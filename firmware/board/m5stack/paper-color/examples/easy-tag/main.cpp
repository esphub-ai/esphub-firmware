// Based on NANANA’s M5PEasyTag for Paper Color v3a1. See NOTICE.txt.
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include <cstdio>

namespace {
constexpr char mountPoint[] = "/sd";
constexpr const char* cards[] = {"/sd/card1.png", "/sd/card2.png", "/sd/card3.png"};

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

enum class Indicator : uint32_t { Off, Menu, Ready, Loading, Missing, Success };
TaskHandle_t indicatorTask;

void showLeds(Indicator state, bool phase)
{
    constexpr RGBColor off{0, 0, 0};
    constexpr RGBColor green{0, 32, 0};
    constexpr RGBColor aqua{0, 64, 64};
    constexpr RGBColor red{64, 0, 0};
    constexpr RGBColor yellow{64, 64, 0};
    RGBColor left = off;
    RGBColor right = off;
    switch (state) {
        case Indicator::Menu:
            left = phase ? green : off;
            break;
        case Indicator::Ready:
            left = green;
            break;
        case Indicator::Loading:
            left = green;
            right = phase ? aqua : off;
            break;
        case Indicator::Missing:
            left = green;
            right = red;
            break;
        case Indicator::Success:
            left = right = yellow;
            break;
        case Indicator::Off:
            break;
    }
    M5.Led.setColor(0, left);
    M5.Led.setColor(1, right);
    M5.Led.display();
}

void driveIndicator(void* owner)
{
    auto state = Indicator::Off;
    bool phase = false;
    for (;;) {
        const bool blink = state == Indicator::Menu || state == Indicator::Loading;
        uint32_t next = 0;
        const bool changed = xTaskNotifyWait(0, 0, &next, blink ? pdMS_TO_TICKS(300) : portMAX_DELAY);
        if (changed) {
            state = static_cast<Indicator>(next);
        }
        phase = changed || !phase;
        showLeds(state, phase);
        if (changed) {
            xTaskNotifyGive(static_cast<TaskHandle_t>(owner));
        }
    }
}

void indicator(Indicator state)
{
    if (indicatorTask) {
        xTaskNotify(indicatorTask, static_cast<uint32_t>(state), eSetValueWithOverwrite);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

void renderMenu(M5Canvas& canvas, const char* missing = nullptr)
{
    canvas.fillSprite(WHITE);
    canvas.setFont(&fonts::Font4);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(BLACK);
    const int middle = canvas.width() / 2;
    canvas.drawString("Paper Color EasyTag", middle, 52);
    canvas.drawString("A: card1.png", middle, 150);
    canvas.drawString("B: card2.png", middle, 208);
    canvas.drawString("C: card3.png", middle, 266);
    if (missing) {
        canvas.setTextColor(RED);
        canvas.drawString("Not found or unreadable:", middle, canvas.height() - 103);
        canvas.drawString(missing, middle, canvas.height() - 72);
        canvas.drawString("Press A, B or C to retry.", middle, canvas.height() - 36);
    }
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
}

bool cardInserted()
{
    auto& pmic = M5.Power.M5pm1;
    using PM1 = m5::M5PM1_Class;
    uint8_t inputs = 0;
    return pmic.begin() &&
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
           pmic.setGPIOPull(PM1::gpio1, PM1::pull_up) &&
           pmic.getGPIOInputBits(&inputs) && !(inputs & (1 << 1));
}

const char* selectedCard()
{
    M5.update();
    if (M5.BtnA.wasReleased()) {
        return cards[0];
    }
    if (M5.BtnB.wasReleased()) {
        return cards[1];
    }
    if (M5.BtnC.wasReleased()) {
        return cards[2];
    }
    return nullptr;
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    M5Canvas canvas(&M5.Display);
    if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
        showError("Display buffer unavailable");
        return;
    }

    M5.Led.setAutoDisplay(false);
    if (M5.Led.getCount() == 2 && M5.Led.begin()) {
        M5.Led.setBrightness(255);
        xTaskCreate(driveIndicator, "tag-led", 4096, xTaskGetCurrentTaskHandle(),
                    tskIDLE_PRIORITY + 1, &indicatorTask);
    }
    indicator(Indicator::Menu);
    renderMenu(canvas);
    indicator(Indicator::Ready);
    std::puts("Paper Color EasyTag ready: A, B or C");
    bool mounted = cardInserted() && mountCard();

    for (;;) {
        const char* path = selectedCard();
        if (!path) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!mounted) {
            mounted = cardInserted() && mountCard();
        }
        indicator(Indicator::Loading);
        canvas.fillSprite(WHITE);
        lgfx::DataWrapperT<FILE> file;
        if (!mounted || !canvas.drawPngFile(&file, path)) {
            indicator(Indicator::Missing);
            renderMenu(canvas, path);
            std::printf("Retry: %s\n", path);
            indicator(Indicator::Ready);
            continue;
        }

        canvas.pushSprite(0, 0);
        M5.Display.waitDisplay();
        std::printf("Tag displayed: %s\n", path);
        indicator(Indicator::Success);
        vTaskDelay(pdMS_TO_TICKS(2000));
        indicator(Indicator::Off);
        std::puts("Entering deep sleep");
        M5.Power.deepSleep();
    }
}
