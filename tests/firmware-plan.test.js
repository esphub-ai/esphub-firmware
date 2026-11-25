import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
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
  const addBoard = (board, flash = 16) => {
    const dir = "firmware/board/" + board;
    put(dir + "/board.json", JSON.stringify({
      chip: "esp32s3", flash_mb: flash, bootloader: "esp32s3-" + flash + "mb",
      default_example: "hello", examples: [{ id: "hello", entrypoint: "examples/hello/main.c" }],
    }));
    put(dir + "/examples/hello/main.c", "hello");
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
    const plans = boardPlans(before, after, options);
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
  const paper = "m5stack/paper-color", watch = "m5stack/stopwatch", other = "waveshare/stopwatch";
  const jobs = ["bootloader", "app", "setup"];
  addBoard(paper);
  addBoard(watch);
  let before = snapshot();
  assert.equal(before[paper].blFingerprint, before[watch].blFingerprint);

  const change = (path, content) => {
    put(path, content);
    const after = snapshot();
    const selected = select(before, after);
    before = after;
    return selected;
  };
  assert.deepEqual(change("firmware/board/" + watch + "/examples/hello/main.c", "new app"), { [watch]: jobs });
  for (const file of ["firmware/components/eh_agent/src/sdk.c", "firmware/ble.defaults",
      "firmware/app/sdkconfig.defaults", "shared/board-examples.js", "tools/ble-wifi/provision_usb.py"]) {
    assert.deepEqual(change(file, "shared input"), { [paper]: jobs, [watch]: jobs });
  }
  assert.deepEqual(change("firmware/board/" + watch + "/README.md", "guide"), {});
  addBoard(other, 8);
  let after = snapshot();
  assert.deepEqual(select(before, after), { [other]: jobs });
  before = after;
  assert.deepEqual(change("firmware/partitions/8mb.csv", "changed table"), { [other]: jobs });
  assert.deepEqual(change("firmware/bootloader/esp32s3-16mb/sdkconfig.defaults", "updated"), {
    [paper]: jobs, [watch]: jobs,
  });
  after = before;
  const [plan] = boardPlans(after, after, { ...options, manual: true, board: other });
  assert.equal(plan.fileStem, "waveshare_stopwatch");
  assert.equal(plan.kit.tag, "firmware-setup-waveshare_stopwatch-v1.1.9");
  assert.equal(plan.kit.app.tag, "firmware-app-waveshare_stopwatch-v1.1.9");
  assert.throws(() => boardPlans({}, after, { ...options, board: "unknown/board" }), /Unknown board/);
  put("firmware/bootloader/esp32s3-4mb/sdkconfig.defaults", "unsupported");
  assert.throws(snapshot, /at least 8 MiB/);
});
