import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { advanceCoverage, verifyCoverage } from "../scripts/firmware-coverage.mjs";

test("a main batch accounts for actual build results and detects cancelled or missing work", () => {
  const cohort = "esp32s3-8mb", board = "m5stack/cardputer-adv";
  const expected = { [cohort]: [[board, ["bootloader", "app", "setup"]]] };
  const shared = { name: cohort + " / bootloader / build", conclusion: "success" };
  const job = (kind) => ({ name: cohort + " / " + board + " / " + kind, conclusion: "success" });
  const boot = job("bootloader"), app = job("app"), kit = job("USB kit"), publish = job("queue-publish");
  const all = [shared, boot, app, kit, publish];
  verifyCoverage(expected, all, "success", true);
  verifyCoverage({}, [], "skipped", true);
  verifyCoverage(expected, [shared, boot, app, kit], "success", false);
  for (const missing of all) {
    assert.throws(() => verifyCoverage(expected, all.filter(j => j !== missing), "failure", true), /Missing/);
  }
  assert.throws(() => verifyCoverage(expected, [], "skipped", true), /Incomplete/);
  assert.throws(() => verifyCoverage({}, [], "cancelled", true), /Incomplete/);

  const compileError = { ...app, conclusion: "failure",
    steps: [{ name: "Copy the board application from the compiled shard", conclusion: "failure" }] };
  verifyCoverage(expected, [shared, boot, compileError], "failure", true);
  assert.throws(() => verifyCoverage(expected, [shared, boot, { ...compileError, steps: [] }], "failure", true), /Missing/);
  assert.throws(() => verifyCoverage(expected, [shared, { ...boot, conclusion: "cancelled" }, compileError], "failure", true), /Incomplete/);
  assert.throws(() => verifyCoverage(expected, [shared, boot, app, kit, { ...publish, conclusion: "cancelled" }], "failure", true), /Incomplete/);
  verifyCoverage(expected, [{ ...shared, conclusion: "failure",
    steps: [{ name: "Compile the shared binaries", conclusion: "failure" }] }], "failure", true);
});

test("out-of-order completions can advance only the coverage ref on the current main history", t => {
  const dir = mkdtempSync(join(tmpdir(), "esphub-main-coverage-"));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const git = (...args) => execFileSync("git", args, { cwd: dir, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
  git("init", "-qb", "main");
  git("init", "--bare", "-q", "remote.git");
  git("remote", "add", "origin", join(dir, "remote.git"));
  function commit(label) {
    git("-c", "user.name=test", "-c", "user.email=test@example.org", "commit", "-qm", label, "--allow-empty");
    return git("rev-parse", "HEAD");
  }
  const first = commit("one"), second = commit("two"), third = commit("three");
  git("push", "-q", "origin", "main");
  const recorded = () => git("--git-dir=remote.git", "rev-parse", "refs/heads/ci/firmware-coverage");
  advanceCoverage(first, "", dir);
  assert.equal(recorded(), first);
  advanceCoverage(third, "", dir); // A concurrent run advanced to first after this one planned.
  advanceCoverage(second, first, dir);
  assert.equal(recorded(), third);
  const offMain = commit("never pushed to main");
  advanceCoverage(offMain, third, dir);
  assert.equal(recorded(), third);
  git("push", "-q", "--force", "origin", first + ":refs/heads/main"); // An actual main rewind.
  advanceCoverage(first, first, dir); // A stale plan cannot acknowledge the rewind.
  assert.equal(recorded(), third);
  advanceCoverage(first, third, dir);
  advanceCoverage(third, first, dir);
  assert.equal(recorded(), first);
  assert.equal(git("--git-dir=remote.git", "rev-parse", "refs/heads/main"), first);
});
