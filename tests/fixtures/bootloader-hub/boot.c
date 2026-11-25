#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "esp_ota_ops.h"
#include "okou_agent.h"
#include "okou_agent_ext.h"
#include "bootloader.h"

void app_main(void);
static bool hold, bootable;
static unsigned selections, consoles, scheduled;
static jmp_buf reboot;
static okou_cmd_handler_t usb_app;

esp_err_t okou_nvs_init(void) { return ESP_OK; }
esp_err_t okou_nvs_set_str(const char *key, const char *value) {
    assert(!strcmp(key, OKOU_KEY_LSLOT) && !strcmp(value, "launcher_b"));
    return ESP_OK;
}
uint8_t okou_nvs_get_u8(const char *key, uint8_t fallback) { assert(!strcmp(key, OKOU_KEY_HOLD)); return hold; }
esp_err_t okou_nvs_erase(const char *key) { assert(!strcmp(key, OKOU_KEY_HOLD)); hold = false; return ESP_OK; }
const esp_partition_t *esp_ota_get_running_partition(void) {
    static const esp_partition_t slot = {.label = "launcher_b"};
    return &slot;
}
void esp_restart(void) { longjmp(reboot, 1); }
esp_err_t okou_select_app(void) { ++selections; return bootable ? ESP_OK : -1; }
void okou_schedule_reboot(uint32_t ms) { ++scheduled; }
void install_reconcile_boot(void) {}
const char *esp_err_to_name(esp_err_t rc) { return "invalid app"; }
int okou_outf(okou_out_t *out, const char *fmt, ...) { return 0; }
esp_err_t okou_cmd_register(const char *name, const char *help, okou_cmd_handler_t fn) {
    assert(!strcmp(name, "app"));
    usb_app = fn;
    return ESP_OK;
}
esp_err_t okou_op_register(const char *name, okou_op_handler_t fn) { return ESP_OK; }
esp_err_t install_op(const cJSON *args, cJSON *data, char *err, size_t len) { return ESP_OK; }
void coredump_ops_register(void) {}
esp_err_t okou_agent_start(const okou_config_t *cfg) { assert(cfg->role == OKOU_ROLE_BOOTLOADER); return ESP_OK; }
void prov_serial_start(void) { ++consoles; }

int main(void) {
    hold = bootable = true;
    app_main();
    assert(!hold && selections == 0 && consoles == 1);
    if (!setjmp(reboot)) {
        app_main();
        assert(0 && "next boot should start the app");
    }
    assert(selections == 1 && consoles == 1);
    bootable = false;
    app_main();
    assert(selections == 2 && consoles == 2);
    assert(usb_app(0, NULL, NULL) != 0 && scheduled == 0);
    bootable = true;
    assert(usb_app(0, NULL, NULL) == 0 && scheduled == 1);
}
