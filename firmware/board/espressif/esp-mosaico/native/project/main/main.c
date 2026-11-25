#include "app_display.h"
#include "app_hello.h"
#include "bundle_gsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "okou_agent.h"

static const char *TAG = "mosaico_hello";
static esp_gsp_handle_t ui;
static app_hello_handle_t controller;

void app_main(void)
{
    const okou_config_t sdk_config = OKOU_CONFIG_DEFAULT();
    esp_err_t err = okou_agent_start(&sdk_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "EspHub SDK startup failed: %s", esp_err_to_name(err));
    }

    esp_gsp_config_t bundle = gsp_bundle_config();
    esp_gsp_esp_lcd_config_t display;
    err = app_display_init(&display);
    if (err == ESP_OK) {
        // Match the BSP's CO5300 SPI transfer limit of eight full-width rows.
        display.display.drawbuf.lines = 8;
        err = esp_gsp_esp_lcd_start(&bundle, &display, &ui);
    }
    if (err == ESP_OK) {
        err = app_hello_create(ui, &controller);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hello World startup failed: 0x%x", (unsigned)err);
        return;
    }
    ESP_LOGI(TAG, "GSP Hello World ready");
}
