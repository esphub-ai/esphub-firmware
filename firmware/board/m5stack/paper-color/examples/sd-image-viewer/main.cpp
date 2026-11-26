// Based on Image Viewer for M5Stack Series v1.0.13. See NOTICE.txt.
#include "eh_agent.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <M5Unified.h>
#include "cJSON.h"
#include "esp_random.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

namespace {
constexpr char mountPoint[] = "/sd";
constexpr size_t maxPictures = 50;

struct Settings {
    bool autoMode = false;
    bool randomInterval = false;
    bool clearBeforeDisplay = false;
    uint32_t intervalMs = 3000;
    uint8_t orientation = 0;
};

Settings readSettings()
{
    Settings settings;
    char buffer[1025]{};
    FILE* file = std::fopen("/sd/image-viewer.json", "rb");
    if (!file) {
        return settings;
    }
    const size_t size = std::fread(buffer, 1, sizeof(buffer), file);
    std::fclose(file);
    if (size == sizeof(buffer)) {
        std::puts("Ignoring oversized image-viewer.json");
        return settings;
    }

    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(buffer, size + 1, nullptr, true), cJSON_Delete);
    if (!cJSON_IsObject(root.get())) {
        std::puts("Ignoring invalid image-viewer.json");
        return settings;
    }
    const auto option = [&root](const char* name) {
        return cJSON_GetObjectItemCaseSensitive(root.get(), name);
    };
    settings.autoMode = cJSON_IsTrue(option("AutoMode"));
    settings.randomInterval = cJSON_IsTrue(option("AutoModeRandomized"));
    settings.clearBeforeDisplay = cJSON_IsTrue(option("ClearBeforeDisplay"));
    const auto* interval = option("AutoModeInterval");
    if (cJSON_IsNumber(interval) && interval->valuedouble >= 1 &&
        interval->valuedouble <= UINT32_MAX) {
        settings.intervalMs = static_cast<uint32_t>(interval->valuedouble);
    }
    const auto* orientation = option("Orientation");
    if (cJSON_IsNumber(orientation) && orientation->valuedouble >= 0 &&
        orientation->valuedouble <= 7) {
        settings.orientation = static_cast<uint8_t>(orientation->valuedouble);
    }
    return settings;
}

enum class Format { Unsupported, Jpeg, Png, Bmp };

Format formatFor(const std::string& name)
{
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return Format::Unsupported;
    }
    const char* ext = name.c_str() + dot;
    if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")) {
        return Format::Jpeg;
    }
    if (!strcasecmp(ext, ".png")) {
        return Format::Png;
    }
    if (!strcasecmp(ext, ".bmp")) {
        return Format::Bmp;
    }
    return Format::Unsupported;
}

std::vector<std::string> findPictures()
{
    std::vector<std::string> pictures;
    DIR* root = opendir(mountPoint);
    if (!root) {
        return pictures;
    }
    while (pictures.size() < maxPictures) {
        const dirent* entry = readdir(root);
        if (!entry) {
            break;
        }
        if (entry->d_type != DT_DIR && formatFor(entry->d_name) != Format::Unsupported) {
            pictures.emplace_back(entry->d_name);
        }
    }
    closedir(root);
    std::sort(pictures.begin(), pictures.end());
    return pictures;
}

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

void showPicture(M5Canvas& canvas, const std::string& name, bool clear)
{
    const auto started = esp_timer_get_time();
    const std::string path = std::string(mountPoint) + '/' + name;
    if (clear) {
        canvas.fillSprite(WHITE);
    }
    lgfx::DataWrapperT<FILE> file;
    bool drawn = false;
    const auto width = canvas.width();
    const auto height = canvas.height();
    switch (formatFor(name)) {
    case Format::Jpeg:
        drawn = canvas.drawJpgFile(&file, path.c_str(), 0, 0, width, height,
                                   0, 0, 0, 0, middle_center);
        break;
    case Format::Png:
        drawn = canvas.drawPngFile(&file, path.c_str(), 0, 0, width, height,
                                   0, 0, 0, 0, middle_center);
        break;
    case Format::Bmp:
        drawn = canvas.drawBmpFile(&file, path.c_str(), 0, 0, width, height,
                                   0, 0, 0, 0, middle_center);
        break;
    case Format::Unsupported:
        break;
    }
    if (drawn) {
        canvas.pushSprite(0, 0);
        std::printf("Displayed: %s at %lld us\n", path.c_str(),
                    static_cast<long long>(started));
    } else {
        showError("Image load failed");
        std::printf("Failed to draw: %s\n", path.c_str());
    }
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(eh_agent_start());
    auto config = M5.config();
    config.clear_display = false;
    M5.begin(config);
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    std::puts("Paper Color microSD Image Viewer");

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
    Settings settings;
    std::vector<std::string> pictures;
    M5Canvas canvas(&M5.Display);
    if (!pinsOk || !pmic.getGPIOInputBits(&inputs)) {
        showError("PM1 init failed");
    } else if (inputs & (1 << 1)) {
        showError("Card not inserted");
    } else {
        std::puts("microSD detected (CARD_DEC low)");
        if (!mountCard()) {
            showError("SD init failed");
        } else {
            settings = readSettings();
            M5.Display.setRotation(settings.orientation);
            M5.update();
            settings.autoMode = settings.autoMode || M5.BtnA.isPressed();
            pictures = findPictures();
            if (pictures.empty()) {
                showError("No JPEG, PNG or BMP images");
            } else if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
                pictures.clear();
                showError("Display buffer unavailable");
            } else {
                canvas.fillSprite(WHITE);
                std::printf("Viewer: %zu images, %s mode, interval %lu ms%s\n",
                            pictures.size(), settings.autoMode ? "automatic" : "manual",
                            static_cast<unsigned long>(settings.intervalMs),
                            settings.randomInterval ? " (randomized)" : "");
                if (!settings.autoMode) {
                    showPicture(canvas, pictures.front(), settings.clearBeforeDisplay);
                }
            }
        }
    }

    size_t index = 0;
    bool firstImagePending = settings.autoMode;
    uint32_t waitMs = settings.intervalMs;
    int64_t lastSwitch = esp_timer_get_time();
    for (;;) {
        M5.update();
        const int64_t now = esp_timer_get_time();
        const bool previous = M5.BtnB.wasClicked();
        const bool next = M5.BtnA.wasClicked() ||
                          (settings.autoMode && (now - lastSwitch) / 1000 >= waitMs);
        if (!pictures.empty() && (previous || next)) {
            if (previous) {
                index = index == 0 ? pictures.size() - 1 : index - 1;
            } else if (!firstImagePending) {
                index = (index + 1) % pictures.size();
            }
            firstImagePending = false;
            lastSwitch = now;
            showPicture(canvas, pictures[index], settings.clearBeforeDisplay);
            if (settings.autoMode && settings.randomInterval) {
                waitMs = esp_random() % settings.intervalMs;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
