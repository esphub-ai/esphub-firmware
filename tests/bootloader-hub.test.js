import test from "node:test";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

test("native USB registration and one-shot bootloader selection", () => {
  const dir = mkdtempSync(join(tmpdir(), "esphub-usb-"));
  const component = "firmware/components/okou_agent";
  try {
    // The production USB parser and SDK hostname validator run natively.
    // Substitute only ESP-IDF types, flash writes and the BLE installation.
    for (const [file, contents] of Object.entries({
      "esp_err.h":
        "#include <stdint.h>\ntypedef int esp_err_t;\n#define ESP_OK 0\n#include <assert.h>\n#define ESP_ERROR_CHECK(e) assert((e) == ESP_OK)\nconst char *esp_err_to_name(esp_err_t);\n",
      "esp_partition.h": "typedef struct { char label[17]; } esp_partition_t;\n",
      "esp_system.h": "void esp_restart(void);",
      "esp_ota_ops.h": '#include "esp_partition.h"\nconst esp_partition_t *esp_ota_get_running_partition(void);',
      "esp_http_client.h":
        "typedef struct esp_http_client *esp_http_client_handle_t;\n",
      "sdkconfig.h": "#define CONFIG_OKOU_BLE_WIFI 1\n",
    }))
      writeFileSync(join(dir, file), "#pragma once\n" + contents);
    const cjson = execFileSync(
      "pkg-config",
      ["--cflags", "--libs", "libcjson"],
      { encoding: "utf8" },
    )
      .trim()
      .split(/\s+/);
    const exe = join(dir, "usb-test");
    const flags = [
      "-std=c11",
      "-Werror",
      "-I" + dir,
      "-I" + component + "/include",
      "-Ifirmware/bootloader/common/main",
    ];
    execFileSync("cc", [
      ...flags,
      component + "/src/okou_hub.c",
      "firmware/bootloader/common/main/prov_common.c",
      "tests/fixtures/bootloader-hub/usb.c",
      ...cjson,
      "-o",
      exe,
    ]);
    execFileSync(exe);
    execFileSync("cc", [
      ...flags,
      "firmware/bootloader/common/main/main.c",
      "tests/fixtures/bootloader-hub/boot.c",
      ...cjson,
      "-o",
      exe,
    ]);
    execFileSync(exe);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
