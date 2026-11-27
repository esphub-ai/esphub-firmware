import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
  mkdtempSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import {
  assignApplicationShards,
  boardPlans,
  buildCohorts,
  profiles,
  firmwareEventContext,
  isMainBuildBatch,
  unchangedMainBoards,
} from "../scripts/firmware-plan.mjs";

test("assigns only selected applications to stable board shards", () => {
  const app = { kind: "app", container: "idf:s3" };
  const board = "m5stack/atoms3";
  const selected = { board, components: [app] };
  const bootloader = { board: "m5stack/paper-color", components: [{ kind: "bootloader" }] };
  const [assignment] = assignApplicationShards([bootloader, selected]);
  assert.equal(assignment.shard, 8);
  assert.deepEqual(assignment.groups, { "idf:s3": [board] });
  assert.equal(selected.appShard, assignment.shard);
  assert.equal(bootloader.appShard, undefined);
  assert.deepEqual(assignApplicationShards([]), []);
});

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
    { manual: false, pullRequest: true, beforeRef: base, afterRef: head },
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
        pullRequest: name === "pull_request",
        beforeRef: before,
        afterRef: head,
      },
    );
  }
});

test("only production pushes and unscoped main dispatches can record coverage", () => {
  const env = { GITHUB_REPOSITORY: "esphub-ai/esphub-firmware", GITHUB_REF: "refs/heads/main", GITHUB_EVENT_NAME: "workflow_dispatch" };
  assert.equal(isMainBuildBatch({}, env), true);
  assert.equal(firmwareEventContext({ inputs: {} }, env).manual, false);
  assert.equal(isMainBuildBatch({}, { ...env, GITHUB_EVENT_NAME: "push" }), true);
  assert.equal(isMainBuildBatch({ inputs: { board: "m5stack/stopwatch" } }, env), false);
  assert.equal(isMainBuildBatch({ inputs: { pr_number: "12" } }, env), false);
  assert.equal(isMainBuildBatch({}, { ...env, GITHUB_EVENT_NAME: "pull_request" }), false);
  assert.equal(isMainBuildBatch({}, { ...env, GITHUB_REF: "refs/heads/topic" }), false);
  assert.equal(isMainBuildBatch({}, { ...env, GITHUB_REPOSITORY: "fork/firmware" }), false);
});

function boardFixture(t) {
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
  const commit = () => {
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
    return git("rev-parse", "HEAD");
  };
  git("init", "-q");
  for (const flash of [8, 16]) {
    put(
      `firmware/bootloader/esp32s3-${flash}mb/sdkconfig.defaults`,
      "defaults",
    );
    put(`firmware/partitions/${flash}mb.csv`, "table");
  }
  return { cwd, git, put, addBoard, commit };
}

test("board CI discovers a new directory and selects only affected tasks", (t) => {
  const { cwd, git, put, addBoard, commit } = boardFixture(t);
  const snapshot = () => profiles(commit(), cwd);
  const options = { version: "1.1.9" };
  const select = (before, after) => Object.fromEntries(
    boardPlans(before, after, options).map(p => [p.board, p.jobs.map(j => j.kind)]),
  );
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
  const covered = git("rev-parse", "HEAD");
  const since = (ref) => select(unchangedMainBoards(ref, git("rev-parse", "HEAD"), before, cwd), before);
  assert.deepEqual(since(covered), {});
  assert.deepEqual(since(""), { [paper]: jobs, [watch]: jobs });
  assert.deepEqual(change("firmware/board/" + watch + "/examples/hello/main.c", "new app"), { [watch]: jobs });
  assert.deepEqual(change("firmware/board/" + watch + "/examples/hello/main.c", "hello"), { [watch]: jobs });
  assert.deepEqual(since(covered), { [watch]: jobs }); // A later push reverted a possibly published change.
  assert.deepEqual(change("firmware/board/" + paper + "/examples/hello/main.c", "paper change"), { [paper]: jobs });
  assert.deepEqual(since(covered), { [paper]: jobs, [watch]: jobs });
  const newer = git("rev-parse", "HEAD");
  git("update-ref", "refs/remotes/origin/main", newer);
  assert.equal(unchangedMainBoards(newer, covered, profiles(covered, cwd), cwd), null);
  git("update-ref", "refs/remotes/origin/main", covered);
  assert.deepEqual(unchangedMainBoards(newer, covered, profiles(covered, cwd), cwd), {});
  assert.deepEqual(change("firmware/board/" + watch + "/examples/hello/sdkconfig.defaults", "CONFIG_X=y"), { [watch]: jobs });
  assert.deepEqual(change("scripts/firmware-plan.mjs", "scheduler only"), {});
  for (const file of ["scripts/build-example.py", "firmware/components/eh_agent/src/sdk.c", "firmware/ble.defaults",
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

test("example-only PRs compile the affected optional examples, with full builds for shared inputs", t => {
  const { cwd, put, addBoard, commit } = boardFixture(t);
  const card = "m5stack/cardputer-adv", watch = "m5stack/stopwatch";
  addBoard(card, 8);
  addBoard(watch);
  const base = commit();
  const dir = "firmware/board/" + card;
  const manifest = JSON.parse(readFileSync(join(cwd, dir, "board.json"), "utf8"));
  const addExample = id => {
    const entrypoint = `examples/${id}/main.c`;
    manifest.examples.push({ id, entrypoint });
    put(dir + "/board.json", JSON.stringify(manifest));
    put(dir + "/" + entrypoint, id);
  };
  function plan(baseSha, headSha) {
    const output = join(cwd, ".git/outputs"), event = join(cwd, ".git/event.json");
    writeFileSync(output, "");
    writeFileSync(event, JSON.stringify({ pull_request: { base: { sha: baseSha } } }));
    const script = fileURLToPath(new URL("../scripts/firmware-plan.mjs", import.meta.url));
    execFileSync(process.execPath, [script], { cwd, env: { ...process.env,
      GITHUB_EVENT_PATH: event, GITHUB_EVENT_NAME: "pull_request", GITHUB_OUTPUT: output,
      FW_HEAD_SHA: headSha, GITHUB_RUN_NUMBER: "17",
    } });
    const lines = readFileSync(output, "utf8").trim().split("\n");
    assert.ok(lines.includes("app_shards=[]"));
    return JSON.parse(lines.find(line => line.startsWith("cohorts=")).slice("cohorts=".length));
  }
  addExample("imu");
  const added = commit();
  const cohorts = plan(base, added);
  assert.equal(cohorts.length, 1);
  const [cohort] = cohorts;
  assert.equal(cohort.name, "examples");
  assert.equal(cohort.bootloader, null);
  assert.deepEqual(cohort.boards.map(b => [b.board, b.examples, b.kit, b.jobs.map(j => j.kind)]),
    [[card, ["imu"], null, ["app"]]]);

  addExample("clock");
  put(dir + "/examples/imu/sdkconfig.defaults", "CONFIG_IMU=y");
  const edited = commit();
  assert.deepEqual(plan(added, edited)[0].boards[0].examples, ["imu", "clock"]);

  manifest.shared_example_inputs = {
    "common/display.hpp": ["imu", "clock"],
    "common/rtc.hpp": ["clock"],
  };
  put(dir + "/board.json", JSON.stringify(manifest));
  put(dir + "/examples/common/display.hpp", "display");
  put(dir + "/examples/common/rtc.hpp", "rtc");
  const scoped = commit();
  assert.deepEqual(plan(base, scoped).map(c => [c.name, c.boards[0].examples]),
    [["examples", ["imu", "clock"]]]);

  put(dir + "/examples/common/rtc.hpp", "updated RTC");
  const rtc = commit();
  assert.deepEqual(plan(scoped, rtc)[0].boards[0].examples, ["clock"]);
  assert.deepEqual(boardPlans(profiles(scoped, cwd), profiles(rtc, cwd), { version: "2.0.17" })
    .map(b => [b.board, b.jobs.map(j => j.kind)]), [[card, ["bootloader", "app", "setup"]]]);

  manifest.shared_example_inputs["common/display.hpp"].push("hello");
  put(dir + "/board.json", JSON.stringify(manifest));
  let previous = commit();
  for (const file of ["examples/common/display.hpp", "examples/common/keyboard.hpp", "examples/hello/main.c"]) {
    put(dir + "/" + file, "changed");
    const next = commit();
    assert.deepEqual(plan(previous, next).flatMap(c => c.boards.map(b => [b.board, b.jobs.map(j => j.kind)])),
      [[card, ["bootloader", "app", "setup"]]]);
    previous = next;
  }
  put("scripts/build-example.py", "shared builder changed");
  const shared = commit();
  assert.deepEqual(plan(added, shared).flatMap(c => c.boards.map(b => b.board)).sort(), [card, watch]);
});

test("overlapping firmware runs share 100 job groups across all bootloader variants", () => {
  const plans = Array.from({ length: 64 }, (_, i) => ({
    board: `test/board-${i}`,
    bootloader: i % 2 ? "s3" : "c6",
    components: [{ kind: "bootloader" }, { kind: "app" }],
  }));
  plans.push({ board: "test/app-only", components: [{ kind: "app" }] });
  const variants = new Map([["s3", { variant: "s3" }], ["c6", { variant: "c6" }]]);
  function groups(runNumber) {
    const cohorts = buildCohorts(plans, variants, "2.0.25", runNumber);
    return cohorts.flatMap(({ bootloader, boards }) => [
      ...(bootloader ? [bootloader.buildGroup] : []),
      ...boards.flatMap(board => board.components.map(task => task.buildGroup)),
    ]);
  }
  const first = groups(99);
  const next = groups(100);
  const expected = new Set(Array.from({ length: 100 }, (_, i) => `firmware-build-slot-${i}`));
  assert.equal(first.length, 131);
  assert.deepEqual(new Set(first), expected);
  assert.deepEqual(new Set(next), expected);
  assert.notEqual(first[0], next[0]);
});
