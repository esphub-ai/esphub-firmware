import test from "node:test";
import { execFileSync } from "node:child_process";
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const scenarios = [
  "idle-online",
  "console-capture",
  "console-fragments",
  "offline-no-ip",
  "unconfigured",
  "boot-ip-wake",
  "queued-events",
  "wake-latch",
  "mqtt-command",
  "command-failure-recovery",
  "event-failure-recovery",
  "hello-failure-recovery",
  "mqtt-disconnect",
  "network-disconnect",
  "connecting-network-disconnect",
  "reconnect-deadline",
  "retry-before-reboot",
  "connecting-reboot",
  "connect-timeout",
  "fresh-connect-timeout",
  "slow-enroll-clock",
  "stable-online-reset",
  "short-online-backoff",
  "reboot-deadline",
  "offline-reboot",
  "fresh-reboot",
  "sub-tick-reboot",
  "failed-publish-reboot-priority",
];

for (const ota of [false, true]) {
  test(`native agent event loop (OTA ${ota ? "enabled" : "disabled"})`, async (t) => {
    const dir = mkdtempSync(join(tmpdir(), "esphub-agent-"));
    const fixture = resolve("tests/fixtures/agent-events");
    try {
      // Forward SDK include names to one small fake boundary; the production
      // agent, public headers, cJSON parser and dispatcher remain unchanged.
      for (const name of [
        "freertos/FreeRTOS.h",
        "freertos/task.h",
        "freertos/queue.h",
        "freertos/semphr.h",
        "freertos/event_groups.h",
        "esp_log.h",
        "esp_timer.h",
        "esp_pm.h",
        "esp_random.h",
        "esp_system.h",
        "esp_mac.h",
        "esp_app_desc.h",
        "esp_idf_version.h",
        "esp_ota_ops.h",
        "esp_heap_caps.h",
        "esp_err.h",
        "esp_partition.h",
        "esp_http_client.h",
        "esp_netif_sntp.h",
        "esp_event.h",
        "esp_netif.h",
        "esp_wifi.h",
        "esp_check.h",
        "sdkconfig.h",
      ]) {
        const path = join(dir, name);
        mkdirSync(resolve(path, ".."), { recursive: true });
        writeFileSync(path, '#pragma once\n#include "fake-idf.h"\n');
      }
      const cjson = execFileSync(
        "pkg-config",
        ["--cflags", "--libs", "libcjson"],
        {
          encoding: "utf8",
        },
      )
        .trim()
        .split(/\s+/);
      const flags = [
        "-std=c11",
        "-D_GNU_SOURCE",
        "-Werror",
        "-g",
        `-DCONFIG_OKL_URL_OTA=${Number(ota)}`,
        "-I" + dir,
        "-I" + fixture,
        "-I" + resolve("."),
        "-Ifirmware/components/okou_agent/include",
      ];
      const exe = join(dir, "agent-test");
      execFileSync(
        "cc",
        [...flags, join(fixture, "agent.c"), ...cjson, "-o", exe],
        {
          stdio: "pipe",
        },
      );
      const cases = ota
        ? [
            ...scenarios,
            "ota-pause-resume",
            "ota-paused-reboot",
            "ota-result-wake",
          ]
        : scenarios;
      for (const name of cases) {
        await t.test(name, () => {
          // One fresh process per scenario resets production statics naturally.
          // Virtual time and setjmp stop the infinite task, never wall-clock sleeps.
          execFileSync(exe, [name], { timeout: 5000, stdio: "pipe" });
        });
      }
      if (!ota) {
        await t.test(
          "largest reboot saturates finite ticks without rebooting early",
          () => {
            const fine = join(dir, "agent-1ms-test");
            execFileSync(
              "cc",
              [
                ...flags,
                "-DconfigTICK_RATE_HZ=1000",
                "-DportTICK_PERIOD_MS=1",
                join(fixture, "agent.c"),
                ...cjson,
                "-o",
                fine,
              ],
              { stdio: "pipe" },
            );
            execFileSync(fine, ["largest-reboot"], {
              timeout: 5000,
              stdio: "pipe",
            });
          },
        );
        await t.test("actual network loss callbacks notify the agent", () => {
          const net = join(dir, "network-test");
          execFileSync(
            "cc",
            [...flags, join(fixture, "net.c"), ...cjson, "-o", net],
            {
              stdio: "pipe",
            },
          );
          execFileSync(net, { timeout: 5000, stdio: "pipe" });
        });
      }
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
}
