/* SPDX-License-Identifier: MIT */
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "okou_agent.h"
#include "okou_agent_ext.h"
#include "bootloader.h"
#include "sdkconfig.h"

static int start_app(int argc, char **argv, okou_out_t *out)
{
    esp_err_t e = okou_select_app();
    if (e != ESP_OK) {
        okou_outf(out, "No bootable app: %s", esp_err_to_name(e));
        return 1;
    }
    okou_schedule_reboot(300);
    return 0;
}

void app_main(void)
{
    ESP_ERROR_CHECK(okou_nvs_init());
    const esp_partition_t *run = esp_ota_get_running_partition();
    ESP_ERROR_CHECK(okou_nvs_set_str(OKOU_KEY_LSLOT, run->label));
    install_reconcile_boot();
    if (okou_nvs_get_u8(OKOU_KEY_HOLD, 0)) {
        ESP_ERROR_CHECK(okou_nvs_erase(OKOU_KEY_HOLD));
    } else if (okou_select_app() == ESP_OK) {
        esp_restart();
    }

#if CONFIG_OKL_USB_CDC
    prov_usb_cdc_init();
#endif
    ESP_ERROR_CHECK(okou_cmd_register("app", "start the installed application", start_app));
    okou_op_register("install", install_op);
    okou_config_t cfg = OKOU_CONFIG_DEFAULT();
    cfg.role = OKOU_ROLE_BOOTLOADER;
    ESP_ERROR_CHECK(okou_agent_start(&cfg));
    prov_serial_start();
}
