/* Bootloader ops `coredump` and `coredump.erase`. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_core_dump.h"
#include "okou_agent_ext.h"
#include "bootloader.h"

static esp_err_t op_coredump(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    size_t addr = 0, size = 0;
    bool present = esp_core_dump_image_get(&addr, &size) == ESP_OK && esp_core_dump_image_check() == ESP_OK;
    cJSON_AddBoolToObject(data, "present", present);
    cJSON *sum = cJSON_AddObjectToObject(data, "summary");
    if (!present) {
        return ESP_OK;
    }
    cJSON_AddNumberToObject(sum, "size", size);
    esp_core_dump_summary_t *s = calloc(1, sizeof(*s));
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
        char hex[16];
        cJSON_AddStringToObject(sum, "task", s->exc_task);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)s->exc_pc);
        cJSON_AddStringToObject(sum, "pc", hex);
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        cJSON *bt = cJSON_AddArrayToObject(sum, "backtrace");
        for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
            snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
            cJSON_AddItemToArray(bt, cJSON_CreateString(hex));
        }
        cJSON_AddBoolToObject(sum, "backtrace_corrupted", s->exc_bt_info.corrupted);
        cJSON_AddNumberToObject(sum, "exc_cause", s->ex_info.exc_cause);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)s->ex_info.exc_vaddr);
        cJSON_AddStringToObject(sum, "exc_vaddr", hex);
#elif CONFIG_IDF_TARGET_ARCH_RISCV
        /* ESP-IDF exposes a stack dump on RISC-V. Unwind the backtrace on the host. */
        cJSON_AddNumberToObject(sum, "mcause", s->ex_info.mcause);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)s->ex_info.mtval);
        cJSON_AddStringToObject(sum, "mtval", hex);
#endif
        char sha[sizeof(s->app_elf_sha256) + 1];
        memcpy(sha, s->app_elf_sha256, sizeof(s->app_elf_sha256));
        sha[sizeof(s->app_elf_sha256)] = '\0';
        cJSON_AddStringToObject(sum, "app_elf_sha256", sha);
        cJSON_AddNumberToObject(sum, "version", s->core_dump_version);
    } else {
        cJSON_AddStringToObject(sum, "error", "summary unavailable");
    }
    free(s);
    return ESP_OK;
}

static esp_err_t op_coredump_erase(const cJSON *args, cJSON *data, char *err, size_t errlen)
{
    esp_err_t e = esp_core_dump_image_erase();
    if (e != ESP_OK) {
        snprintf(err, errlen, "erase failed: %s", esp_err_to_name(e));
    }
    return e;
}

void coredump_ops_register(void)
{
    okou_op_register("coredump", op_coredump);
    okou_op_register("coredump.erase", op_coredump_erase);
}
