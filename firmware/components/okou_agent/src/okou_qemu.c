/*
 * QEMU-only: Espressif QEMU (esp_develop_9.0.0_20240606) does not reset peripherals on the
 * SW_CPU_RESET that esp_restart()/panic use, and the next boot takes a stale interrupt in
 * esp_timer_init (InstrFetchProhibited, PC=0). With CONFIG_OKOU_QEMU_SYSTEM_RESET the link
 * wraps esp_restart_noos() so every restart (incl. panic) is a full system reset instead.
 * Never enabled for hardware builds.
 * SPDX-License-Identifier: MIT
 */
#include "sdkconfig.h"
#if CONFIG_OKOU_QEMU_SYSTEM_RESET
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_private/system_internal.h"

void IRAM_ATTR __attribute__((noreturn)) __wrap_esp_restart_noos(void)
{
    esp_rom_software_reset_system();
    while (1) {
    }
}
#endif
