/* ESP Hub MicroPython runtime bindings. SPDX-License-Identifier: MIT */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_pm.h"

#include "cJSON.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/builtin.h"

#include "shared/runtime/interrupt_char.h"
#include "shared/readline/readline.h" /* CHAR_CTRL_C */

#include "okou_agent.h"
#include "okou_agent_ext.h"
#include "okou_install.h"

static const char *TAG = "okl_py";

void mp_hal_wake_main_task(void);

static void interrupt_running(void)
{
    if (mp_interrupt_char == CHAR_CTRL_C) { /* code is running (not waiting at the REPL) */
        mp_sched_keyboard_interrupt();
    }
    mp_hal_wake_main_task();
}

static esp_err_t op_py_interrupt(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    cJSON_AddBoolToObject(data, "running", mp_interrupt_char == CHAR_CTRL_C);
    interrupt_running();
    return ESP_OK;
}

static esp_err_t op_py_safe_boot(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    okou_nvs_set_u8("pysafe", 1);
    okou_schedule_reboot(300);
    return ESP_OK;
}

/* ------------------------------------------------------------------ start hook */

// Called by the TinyUSB task for native USB-OTG boards. USB-Serial/JTAG
// has its own cable-aware IDF power lock.
void okl_usb_bus_active(bool active)
{
    static esp_pm_lock_handle_t usb_lock;
    static bool held;
    if (active == held) return;
    if (!usb_lock && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb-otg", &usb_lock) != ESP_OK)
        return;
    esp_err_t result = active ? esp_pm_lock_acquire(usb_lock) : esp_pm_lock_release(usb_lock);
    if (result == ESP_OK) held = active;
}

void okl_runtime_start(void)
{
    ESP_ERROR_CHECK(okou_nvs_init());
    okou_op_register("py.interrupt", op_py_interrupt);
    okou_op_register("py.safe_boot", op_py_safe_boot);
    okou_config_t cfg = OKOU_CONFIG_DEFAULT(); /* role app, SDK-managed Wi-Fi */
    cfg.micropython_version = MICROPY_VERSION_STRING;
    esp_err_t e = okou_agent_start(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "okou_agent_start: %s", esp_err_to_name(e));
    }
}

/* ------------------------------------------------------------------ Python module */

static mp_obj_t okou_connected_py(void)
{
    return mp_obj_new_bool(okou_connected());
}
static MP_DEFINE_CONST_FUN_OBJ_0(okou_connected_obj, okou_connected_py);

static mp_obj_t okou_event_py(size_t n_args, const mp_obj_t *args)
{
    okou_event(mp_obj_str_get_str(args[0]), n_args > 1 && args[1] != mp_const_none ? mp_obj_str_get_str(args[1]) : NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(okou_event_obj, 1, 2, okou_event_py);

static mp_obj_t okou_safe_boot_py(void)
{
    bool safe = okou_nvs_get_u8("pysafe", 0) != 0;
    if (safe) {
        okou_nvs_set_u8("pysafe", 0);
    }
    return mp_obj_new_bool(safe);
}
static MP_DEFINE_CONST_FUN_OBJ_0(okou_safe_boot_obj, okou_safe_boot_py);

static mp_obj_t okou_wifi_rssi_py(void)
{
    int rssi;
    return okou_wifi_rssi(&rssi) ? mp_obj_new_int(rssi) : mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(okou_wifi_rssi_obj, okou_wifi_rssi_py);

static mp_obj_t okou_enter_bootloader_py(void)
{
    esp_err_t rc = okou_enter_bootloader();
    mp_raise_OSError(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_0(okou_enter_bootloader_obj, okou_enter_bootloader_py);

static mp_obj_t okou_wifi_join_py(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    enum { ARG_ssid, ARG_password, ARG_hidden };
    static const mp_arg_t allowed[] = {
        {MP_QSTR_ssid, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_password, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_hidden, MP_ARG_BOOL, {.u_bool = false}},
    };
    mp_arg_val_t values[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, args, kw_args, MP_ARRAY_SIZE(allowed), allowed, values);
    size_t ssid_len, password_len;
    const char *ssid = mp_obj_str_get_data(values[ARG_ssid].u_obj, &ssid_len);
    const char *password = mp_obj_str_get_data(values[ARG_password].u_obj, &password_len);
    if (memchr(ssid, 0, ssid_len) || memchr(password, 0, password_len))
        mp_raise_ValueError(MP_ERROR_TEXT("Wi-Fi credentials contain NUL"));
    char network[33], secret[65];
    if (!ssid_len || ssid_len >= sizeof(network) || password_len >= sizeof(secret))
        return mp_const_false;
    memcpy(network, ssid, ssid_len);
    network[ssid_len] = '\0';
    memcpy(secret, password, password_len);
    secret[password_len] = '\0';
    bool queued = okou_wifi_join(network, secret, values[ARG_hidden].u_bool);
    volatile char *wipe = secret;
    for (size_t i = 0; i < sizeof(secret); i++) wipe[i] = 0;
    return mp_obj_new_bool(queued);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(okou_wifi_join_obj, 0, okou_wifi_join_py);

static mp_obj_t okou_wifi_join_state_py(void)
{
    const char *state = okou_wifi_join_state();
    return mp_obj_new_str(state, strlen(state));
}
static MP_DEFINE_CONST_FUN_OBJ_0(okou_wifi_join_state_obj, okou_wifi_join_state_py);

static const mp_rom_map_elem_t okou_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_enter_bootloader), MP_ROM_PTR(&okou_enter_bootloader_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_join), MP_ROM_PTR(&okou_wifi_join_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_join_state), MP_ROM_PTR(&okou_wifi_join_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_rssi), MP_ROM_PTR(&okou_wifi_rssi_obj) },
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_okou) },
    { MP_ROM_QSTR(MP_QSTR_connected), MP_ROM_PTR(&okou_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_event), MP_ROM_PTR(&okou_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_safe_boot), MP_ROM_PTR(&okou_safe_boot_obj) },
};
static MP_DEFINE_CONST_DICT(okou_module_globals, okou_module_globals_table);

const mp_obj_module_t okou_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&okou_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_okou, okou_module);
