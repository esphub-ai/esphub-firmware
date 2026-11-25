import test from "node:test";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

// Compile the production manager, not a reimplementation. The fixture replaces
// only ESP-IDF/FreeRTOS, NVS and zeroization; each scenario gets fresh C statics.
test("native Wi-Fi scan hints and safe fallback", async (t) => {
  const dir = mkdtempSync(join(tmpdir(), "esphub-wifi-"));
  try {
    const cjson = execFileSync(
      "pkg-config",
      ["--cflags", "--libs", "libcjson"],
      { encoding: "utf8" },
    )
      .trim()
      .split(/\s+/);
    const exe = join(dir, "wifi-test");
    execFileSync("cc", [
      "-std=c11",
      "-D_POSIX_C_SOURCE=200809L",
      "-Wall",
      "-Wextra",
      "-Werror",
      "-Wno-unused-parameter",
      "-Itests/fixtures/wifi",
      "-Ifirmware/components/okou_agent/include",
      "-Ifirmware/components/okou_agent/src",
      "-include",
      "tests/fixtures/wifi/compat.h",
      "firmware/components/okou_agent/src/okou_wifi.c",
      "tests/fixtures/wifi/radio.c",
      ...cjson,
      "-o",
      exe,
    ]);
    for (const [scenario, description] of [
      ["scan", "deduplicate/sort scans with coherent strongest-AP metadata"],
      ["fresh", "use fresh channel+BSSID with FAST_SCAN without a full retry"],
      ["order", "try visible saved SSIDs strongest first, not saved/MRU order"],
      ["other-ap", "unpin and discover another AP with the same SSID"],
      ["channel-moved", "unpin and discover the same AP on a changed channel"],
      ["hidden", "retain hidden saved-network fallback after visible failures"],
      ["unseen", "connect unseen saved SSIDs without inheriting prior pins"],
      ["connect-error", "fall back after immediate connect failure"],
      [
        "config-error",
        "never connect with stale config after set_config failure",
      ],
      ["timeout", "skip fallback when the shared 15s budget is exhausted"],
      ["budget", "give same-SSID fallback only the remaining 15s budget"],
      [
        "budget-wrap",
        "preserve the shared deadline across tick-count wraparound",
      ],
      [
        "budget-fallback-timeout",
        "expire fallback at the original shared deadline",
      ],
      ["budget-last-tick", "allow fallback even with only one tick remaining"],
      [
        "budget-disc-expired",
        "skip fallback after a disconnect at the deadline",
      ],
      ["scan-error", "use saved order and no hints when the scan fails"],
      ["rescan-error", "never reuse hints from an earlier connection cycle"],
      ["manual", "manual joins ignore even a previous public scan"],
      [
        "manual-hidden",
        "hidden manual joins stay unpinned and save on success",
      ],
      ["manual-failed", "failed manual joins preserve saved-network access"],
      [
        "save-failed",
        "failed persistence rolls back and restores saved access",
      ],
      ["queued", "a queued manual join preempts the hinted retry"],
    ]) {
      await t.test(description, () => execFileSync(exe, [scenario]));
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
