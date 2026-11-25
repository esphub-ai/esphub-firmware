import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
  appendFileSync,
  cpSync,
  mkdtempSync,
  mkdirSync,
  writeFileSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import test from "node:test";
import {
  boardPlans,
  profiles,
  firmwareEventContext,
} from "../scripts/firmware-plan.mjs";

test("firmware planning uses the dispatched PR snapshots without a manual rebuild", () => {
  const base = "a".repeat(40),
    head = "b".repeat(40);
  assert.deepEqual(
    firmwareEventContext(
      { inputs: { pr_number: "49" } },
      {
        GITHUB_EVENT_NAME: "workflow_dispatch",
        PR_CI_BASE_SHA: base,
        GITHUB_SHA: head,
      },
    ),
    { manual: false, beforeRef: base, afterRef: head },
  );
  assert.throws(
    () =>
      firmwareEventContext(
        { inputs: { pr_number: "49" } },
        {
          GITHUB_EVENT_NAME: "workflow_dispatch",
          GITHUB_SHA: head,
        },
      ),
    /validated base SHA/,
  );
  for (const [name, event, manual, before] of [
    ["pull_request", { pull_request: { base: { sha: base } } }, false, base],
    ["push", { before: base }, false, base],
    ["workflow_dispatch", { inputs: { board: "m5stack/stopwatch" } }, true, undefined],
  ]) {
    assert.deepEqual(
      firmwareEventContext(event, {
        GITHUB_EVENT_NAME: name,
        GITHUB_SHA: head,
      }),
      {
        manual,
        beforeRef: before,
        afterRef: head,
      },
    );
  }
});

test("board CI discovers a new directory and selects only affected tasks", (t) => {
  const cwd = mkdtempSync(join(tmpdir(), "esphub-board-plan-"));
  t.after(() => rmSync(cwd, { recursive: true, force: true }));
  const git = (...args) =>
    execFileSync("git", args, { cwd, encoding: "utf8" }).trim();
  const put = (path, data) => {
    mkdirSync(dirname(join(cwd, path)), { recursive: true });
    writeFileSync(join(cwd, path), data);
  };
  const addBoard = (model, vendor = "m5stack") => {
    const dir = `firmware/board/${vendor}/${model}`;
    put(
      dir + "/board.json",
      JSON.stringify({
        chip: "esp32s3",
        flash_mb: 16,
        bootloader: "esp32s3-16mb",
      }),
    );
    put(dir + "/layout/partitions.csv", "table");
    put(dir + "/ci.json", JSON.stringify({ micropython: {}, setup: {} }));
    for (const kind of ["micropython", "setup"])
      put(dir + "/" + kind + "/action.yml", "runs: { using: composite }");
  };
  const snapshot = () => {
    git("add", ".");
    git(
      "-c",
      "user.name=test",
      "-c",
      "user.email=test@example.org",
      "commit",
      "-qm",
      "fixture",
    );
    return profiles(git("rev-parse", "HEAD"), cwd);
  };
  const options = { version: "1.1.9" };
  const select = (before, after) => {
    const releases = Object.fromEntries(
      Object.entries(before).map(([board, profile]) => [
        board,
        Object.fromEntries(
          Object.entries(profile.tasks).map(([kind, task]) => [
            kind,
            {
              fingerprint: task.fingerprint,
              tag: kind + "-v1.0.1",
              version: "1.0.1",
            },
          ]),
        ),
      ]),
    );
    const plans = boardPlans(before, after, releases, options);
    return Object.fromEntries(
      plans.map((p) => [p.board, p.jobs.map((j) => j.kind)]),
    );
  };
  git("init", "-q");
  for (const flash of [8, 16]) {
    put(
      `firmware/bootloader/esp32s3-${flash}mb/sdkconfig.defaults`,
      "defaults",
    );
    put(`firmware/partitions/${flash}mb.csv`, "table");
  }
  addBoard("paper-color");
  addBoard("stopwatch");
  let before = snapshot();
  put("firmware/board/m5stack/stopwatch/layout/partitions.csv", "new table");
  let after = snapshot();
  assert.deepEqual(select(before, after), { "m5stack/stopwatch": ["bootloader", "setup"] });
  assert.equal(after["m5stack/stopwatch"].blFingerprint, before["m5stack/stopwatch"].blFingerprint);
  assert.equal(
    after["m5stack/stopwatch"].blFingerprint,
    after["m5stack/paper-color"].blFingerprint,
  );
  before = after;
  put(
    "firmware/board/m5stack/stopwatch/micropython/profile.json",
    '{"target":"upstream"}',
  );
  after = snapshot();
  assert.deepEqual(select(before, after), {
    "m5stack/stopwatch": ["micropython", "setup"],
  });
  before = after;
  put(
    "firmware/board/m5stack/stopwatch/setup/action.yml",
    "new packaging recipe",
  );
  after = snapshot();
  assert.deepEqual(select(before, after), { "m5stack/stopwatch": ["setup"] });
  before = after;
  const advDir = "firmware/board/m5stack/cardputer-adv";
  cpSync(new URL("../" + advDir, import.meta.url), join(cwd, advDir), {
    recursive: true,
    verbatimSymlinks: true,
  });
  after = snapshot();
  assert.deepEqual(select(before, after), {
    "m5stack/cardputer-adv": ["bootloader", "micropython", "setup"],
  });
  before = after;
  put("firmware/bootloader/esp32s3-8mb/sdkconfig.defaults", "new defaults");
  after = snapshot();
  assert.deepEqual(select(before, after), {
    "m5stack/cardputer-adv": ["bootloader", "setup"],
  });
  assert.equal(after["m5stack/stopwatch"].blFingerprint, before["m5stack/stopwatch"].blFingerprint);
  before = after;
  appendFileSync(
    join(cwd, advDir, "micropython/manifest.py"),
    "\n# test keyboard driver change\n",
  );
  after = snapshot();
  assert.deepEqual(select(before, after), {
    "m5stack/cardputer-adv": ["micropython", "setup"],
  });
  before = after;
  put("firmware/components/okou_agent/src/sdk.c", "shared");
  after = snapshot();
  assert.deepEqual(
    Object.values(select(before, after)),
    Array(3).fill(["bootloader", "micropython", "setup"]),
  );
  before = after;
  put("firmware/ble.defaults", "CONFIG_BT_ENABLED=y");
  after = snapshot();
  assert.deepEqual(
    Object.values(select(before, after)),
    Array(3).fill(["bootloader", "micropython", "setup"]),
  );
  before = after;
  put("services/web/src/app.js", "website");
  put("firmware/board/m5stack/stopwatch/README.md", "guide");
  after = snapshot();
  assert.deepEqual(select(before, after), {});
  before = after;
  put("firmware/board/m5stack/stopwatch/main.py", "print('hello')");
  after = snapshot();
  assert.deepEqual(select(before, after), { "m5stack/stopwatch": ["setup"] });
  before = after;
  addBoard("stopwatch", "waveshare");
  after = snapshot();
  assert.deepEqual(select(before, after), {
    "waveshare/stopwatch": ["bootloader", "micropython", "setup"],
  });
  const [selected] = boardPlans({}, after, {}, { ...options, board: "waveshare/stopwatch" });
  assert.equal(selected.board, "waveshare/stopwatch");
  assert.equal(selected.fileStem, "waveshare_stopwatch");
  assert.equal(selected.kit.tag, "firmware-setup-waveshare_stopwatch-v1.1.9");
  assert.equal(
    after["waveshare/stopwatch"].blFingerprint,
    after["m5stack/stopwatch"].blFingerprint,
  );
  const released = Object.fromEntries(
    Object.entries(after).map(([name, p]) => [name, p.tasks]),
  );
  assert.deepEqual(
    boardPlans({}, after, released, { ...options, manual: true }),
    [],
  );
  put("firmware/bootloader/esp32s3-4mb/sdkconfig.defaults", "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y");
  assert.throws(snapshot, /at least 8 MiB/);
});
