#!/usr/bin/env python3
"""Turn a UIFlow2 MicroPython checkout (m5stack/uiflow-micropython) into the esp-bootloader
Python runtime for one board. Every edit asserts its anchor, so an upstream change fails the
build instead of silently producing a different firmware.

  apply_overlay.py <uiflow checkout> <EspHub firmware dir> <group/project> <version>
"""
import json
import re
import pathlib
import shutil
import sys

uiflow_dir, firmware_dir, board_id, version = sys.argv[1:]
uiflow, fw = pathlib.Path(uiflow_dir), pathlib.Path(firmware_dir)
board_dir = fw / "board" / board_id
config = json.loads((board_dir / "micropython/profile.json").read_text())
board = config["target"]
metadata = json.loads((board_dir / "board.json").read_text())
bootloader = metadata["bootloader"]
generic = board.startswith("ESP32_GENERIC")
mp = uiflow / "micropython"
m5 = mp / "ports/esp32" if generic else uiflow / "m5stack"
ov = fw / "pyrt" / "overlay"


def edit(path, old, new, count=1):
    p = m5 / path
    s = p.read_text()
    assert s.count(old) >= 1, f"{path}: anchor not found: {old!r}"
    p.write_text(s.replace(old, new, count))
    print(f"edited {path}")


# 1. okou_agent SDK component, the `okou` C module, our boot + frozen manifest, build table.
shutil.copytree(fw / "components" / "okou_agent", m5 / "components" / "okou_agent", dirs_exist_ok=True)
shutil.copytree(ov / "cmodules" / "okou", m5 / "cmodules" / "okou", dirs_exist_ok=True)
shutil.copytree(ov / "okl", m5 / "okl", dirs_exist_ok=True)
shutil.copy(ov / "partitions_okl_build.csv", m5 / "partitions_okl_build.csv")
if generic:
    # ESP-IDF already supplies Unix seconds, as the UIFlow TLS clock expects.
    # Applying the 2000 epoch offset makes valid MQTT certificates appear expired.
    edit("main.c", "return tv.tv_sec + TIMEUTILS_SECONDS_1970_TO_2000;", "return tv.tv_sec;")
    edit("esp32_common.cmake", "list(APPEND IDF_COMPONENTS", "list(APPEND IDF_COMPONENTS\n    okou_agent")
    # Keep OTA safe-boot behavior identical to UIFlow's main loop.
    uiflow_main = (uiflow / "m5stack/main.c").read_text()
    safe_gate = re.search(r"static bool uiflow_should_run_main\(void\) \{.*?\n}", uiflow_main, re.S)[0]
    edit("main.c", "int vprintf_null(", safe_gate + "\n\nint vprintf_null(")
    edit("main.c", "&& ret != 0)", "&& ret != 0 && uiflow_should_run_main())")
else:
    (m5 / "cmodules" / "cmodules.cmake").open("a").write(
        "\n# esp-bootloader Python runtime\ninclude(${CMAKE_CURRENT_LIST_DIR}/okou/micropython.cmake)\n")
    print("edited cmodules/cmodules.cmake")
    edit("CMakeListsDefault.cmake", "    esp_driver_ppa\n)", "    esp_driver_ppa\n    okou_agent\n)")

# 2. Start the agent (Wi-Fi, hub) from C before the MicroPython task exists.
edit("main.c", "void MICROPY_ESP_IDF_ENTRY(void) {",
     "void okl_runtime_start(void); /* esp-launcher: cmodules/okou/modokou.c */\n\nvoid MICROPY_ESP_IDF_ENTRY(void) {")
edit("main.c", "    MICROPY_BOARD_STARTUP();\n",
     "    MICROPY_BOARD_STARTUP();\n    okl_runtime_start();\n")

# Both UIFlow and the generic ESP32 port build this stdout implementation.
# Explicit file objects in print(file=...) keep their own stream behavior.
stdout = 'mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {'
edit(mp.resolve() / "ports/esp32/mphalport.c", stdout,
     '#include "okou_agent.h"\n' + stdout + '\n    if (okou_console_write(str, len)) return len;')

# pyexec writes the traceback to stdout and reports the failure event to the hub.
edit("main.c", '#include "py/runtime.h"', '#include "py/runtime.h"\n#include "okou_agent.h"')
for script, indent in (("boot.py", "    "), ("main.py", "        ")):
    call = f'int ret = pyexec_file_if_exists("{script}");'
    edit("main.c", call, call + f'\n{indent}if (ret == PYEXEC_UNHANDLED_EXCEPTION) okou_event("program.failed", "{script}");')

# Keep raw and friendly REPL upload modes out of the production USB console.
edit("main.c", """    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            vprintf_like_t vprintf_log = esp_log_set_vprintf(vprintf_null);
            if (pyexec_raw_repl() != 0) {
                break;
            }
            esp_log_set_vprintf(vprintf_log);
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }
""", """    /* Applications can read USB input. Finished programs idle with VM callbacks
     * enabled while the native agent remains available for OTA and recovery. */
    for (;;) {
        mp_handle_pending(false);
        MP_THREAD_GIL_EXIT();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        MP_THREAD_GIL_ENTER();
    }
""")

# Keep the native USB-OTG link awake while it enumerates or carries traffic.
# TinyUSB dispatches bus events on the VM task, so this needs no USB polling.
for source in ("mp_usbd.c", "mp_usbd_runtime.c"):
    edit(mp.resolve() / "shared/tinyusb" / source, "    tud_task_ext(0, false);",
         "    tud_task_ext(0, false);\n"
         "    extern void okl_usb_bus_active(bool active);\n"
         "    okl_usb_bus_active(tud_connected() && !tud_suspended());")

# 3. Board: our sdkconfig fragment last, our frozen manifest, no Japanese/Korean 24 px fonts.
bdir = f"boards/{board}"
extra_manifest = board_dir / "micropython/manifest.py"
if extra_manifest.exists():
    with (m5 / "okl/manifest.py").open("a") as manifest:
        manifest.write("\n" + extra_manifest.read_text())
for option in config.get("disable_cmake_options", []):
    path = m5 / bdir / "mpconfigboard.cmake"
    text, count = re.subn(rf"set\({re.escape(option)} (?:1|TRUE)\)", f"set({option} 0)", path.read_text())
    assert count == 1, f"{path}: expected enabled {option}"
    path.write_text(text)
frag = (ov / "sdkconfig.okl").read_text() + f'CONFIG_APP_PROJECT_VER="{version}"\n'
frag += (fw / "ble.defaults").read_text()
extra_config = board_dir / "micropython/sdkconfig.defaults"
if extra_config.exists():
    frag += "\n" + extra_config.read_text()
frag += f'\nCONFIG_ESPTOOLPY_FLASHSIZE_{metadata["flash_mb"]}MB=y\n'
# The provisioning service owns NimBLE during boot discovery and provisioning.
edit(mp.resolve() / "extmod/nimble/modbluetooth_nimble.c", "int mp_bluetooth_init(void) {",
     "extern bool okou_ble_host_owned(void);\nint mp_bluetooth_init(void) {\n"
     "    if (okou_ble_host_owned()) return MP_EBUSY;")

(m5 / bdir / "sdkconfig.okl").write_text(frag + f'CONFIG_OKOU_BOARD_PROFILE="{board_id}"\nCONFIG_OKOU_BL_PROFILE="{bootloader}"\n')
if generic:
    # Start from the chip port so boards without a UIFlow target never probe
    # an unrelated display, camera or power-management IC at startup.
    cmake = m5 / bdir / "mpconfigboard.cmake"
    with cmake.open("a") as out:
        if config.get("psram"):
            out.write(f'\nlist(APPEND SDKCONFIG_DEFAULTS boards/sdkconfig.{config["psram"]})\n')
        out.write(f'\nlist(APPEND SDKCONFIG_DEFAULTS {bdir}/sdkconfig.okl)\n')
    header = m5 / bdir / "mpconfigboard.h"
    text, count = re.subn(r'(#define MICROPY_HW_BOARD_NAME\s+)"[^"]+"',
                          rf'\g<1>"{metadata["model"]}"', header.read_text())
    assert count == 1, f"{board}: missing hardware name"
    header.write_text(text)
    (m5 / "okl/manifest.py").write_text(
        'module("_boot.py", base_path="$(PORT_DIR)/okl")\n'
        'include("$(MPY_DIR)/extmod/asyncio")\nrequire("ssl")\n')
    print("overlay applied for", board, version)
    sys.exit(0)

cmake = m5 / bdir / "mpconfigboard.cmake"
text, count = re.subn(r"(set\(SDKCONFIG_DEFAULTS\b[^)]*)\)", rf"\1    {bdir}/sdkconfig.okl\n)", cmake.read_text())
assert count == 1, f"{board}: missing SDKCONFIG_DEFAULTS"
for font in ("FONT_ALIBABASANS_JA24", "FONT_ALIBABASANS_KR24"):
    text, count = re.subn(rf"set\({font} [01]\)", f"set({font} 0)", text)
    assert count == 1, f"{board}: missing {font}"
cmake.write_text(text)
edit(f"{bdir}/mpconfigboard.cmake", "if(NOT MICROPY_FROZEN_MANIFEST)",
     "set(MICROPY_FROZEN_MANIFEST ${CMAKE_SOURCE_DIR}/okl/manifest.py)\nif(NOT MICROPY_FROZEN_MANIFEST)")

# 4. Font flags only hid the fonts from Python; also drop their data and objects.
edit("components/M5Unified/CMakeLists.txt",
     "    USR_FONT_ALIBABASANS_KR24=${FONT_ALIBABASANS_KR24}\n)",
     "    USR_FONT_ALIBABASANS_KR24=${FONT_ALIBABASANS_KR24}\n"
     "    ALIBABASANS_JP24=${FONT_ALIBABASANS_JA24}\n"
     "    ALIBABASANS_KR24=${FONT_ALIBABASANS_KR24}\n)")
# Unit C6L exposes the large efont tables directly under two Python names
# per locale. Its board-specific branch otherwise ignores the font flags.
if board == "M5STACK_Unit_C6L":
    for path, expected_lines in (("components/M5Unified/mpy_m5gfx.cpp", 3),
                                 ("cmodules/m5unified/m5unified_gfx.c", 6)):
        source = m5 / path
        text, count = re.subn(r"^.*gfx_font_efont(?:CN|JA|KR)_24_obj.*\n", "", source.read_text(), flags=re.M)
        assert count == expected_lines, f"{path}: unmatched Unit C6L bitmap fonts"
        source.write_text(text)
edit("components/M5Unified/mpy_m5gfx.cpp",
     "// FONT_ALIBABASANS_JA24\nextern const lv_font_t AlibabaSans_JP24;\n"
     "const M5LvglFont AlibabaSans_JP24_obj(&AlibabaSans_JP24);\n"
     "const font_obj_t gfx_font_AlibabaSans_JP24_obj = {{ &mp_type_object }, &AlibabaSans_JP24_obj };\n",
     "#if USR_FONT_ALIBABASANS_JA24\n// FONT_ALIBABASANS_JA24\nextern const lv_font_t AlibabaSans_JP24;\n"
     "const M5LvglFont AlibabaSans_JP24_obj(&AlibabaSans_JP24);\n"
     "const font_obj_t gfx_font_AlibabaSans_JP24_obj = {{ &mp_type_object }, &AlibabaSans_JP24_obj };\n#endif\n")
edit("components/M5Unified/mpy_m5gfx.cpp",
     "// FONT_ALIBABASANS_KR24\nextern const lv_font_t AlibabaSans_KR24;\n"
     "const M5LvglFont AlibabaSans_KR24_obj(&AlibabaSans_KR24);\n"
     "const font_obj_t gfx_font_AlibabaSans_KR24_obj = {{ &mp_type_object }, &AlibabaSans_KR24_obj };\n",
     "#if USR_FONT_ALIBABASANS_KR24\n// FONT_ALIBABASANS_KR24\nextern const lv_font_t AlibabaSans_KR24;\n"
     "const M5LvglFont AlibabaSans_KR24_obj(&AlibabaSans_KR24);\n"
     "const font_obj_t gfx_font_AlibabaSans_KR24_obj = {{ &mp_type_object }, &AlibabaSans_KR24_obj };\n#endif\n")
print("overlay applied for", board, version)
