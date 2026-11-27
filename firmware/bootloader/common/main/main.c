/* SPDX-License-Identifier: MIT */
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "eh_agent.h"
#include "eh_agent_ext.h"
#include "bootloader.h"
#include "sdkconfig.h"

static int start_app(int argc, char **argv, eh_out_t *out)
{
    esp_err_t e = eh_select_app();
    if (e != ESP_OK) {
        eh_outf(out, "No bootable app: %s", esp_err_to_name(e));
        return 1;
    }
    eh_schedule_reboot(300);
    return 0;
}

void app_main(void)
{
    ESP_ERROR_CHECK(eh_nvs_init());
    const esp_partition_t *run = esp_ota_get_running_partition();
    ESP_ERROR_CHECK(eh_nvs_set_str(EH_KEY_LSLOT, run->label));
    reconcile_resize();
    eh_install_reconcile_boot();
    if (eh_nvs_get_u8(EH_KEY_HOLD, 0)) {
        ESP_ERROR_CHECK(eh_nvs_erase(EH_KEY_HOLD));
    } else if (eh_select_app() == ESP_OK) {
        esp_restart();
    }

    ESP_ERROR_CHECK(eh_cmd_register("app", "start the installed application", start_app));
    ESP_ERROR_CHECK(eh_op_register("install", launcher_install));
    ESP_ERROR_CHECK(eh_op_register("resize_app", resize_app));
    ESP_ERROR_CHECK(eh_agent_start());
    report_layout();
    prov_serial_start();
}
