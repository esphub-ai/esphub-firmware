import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { advanceCoverage, verifyCoverage } from "../scripts/firmware-coverage.mjs";

test("a main batch accounts for actual build results and detects cancelled or missing work", () => {
  const cohort = "esp32s3-8mb", board = "m5stack/cardputer-adv";
  const expected = { [cohort]: { bootloader: true, boards: [[board, ["app"]]] } };
  const named = (name, conclusion = "success") => ({ name: cohort + " / " + name, conclusion });
  const set = named("bootloader / build"), setPublish = named("bootloader / queue-publish");
  const app = named(board + " / app"), publish = named(board + " / queue-publish");
  const all = [set, setPublish, app, publish];
  verifyCoverage(expected, all, "success", true);
  verifyCoverage({}, [], "skipped", true);
  verifyCoverage(expected, [set, app], "success", false);
  for (const missing of all) {
    assert.throws(() => verifyCoverage(expected, all.filter(j => j !== missing), "failure", true), /Missing/);
  }
  assert.throws(() => verifyCoverage(expected, [], "skipped", true), /Incomplete/);
  assert.throws(() => verifyCoverage({}, [], "cancelled", true), /Incomplete/);

  // A cohort whose bootloader set did not change builds only its boards.
  verifyCoverage({ [cohort]: { bootloader: false, boards: [[board, ["app"]]] } }, [app, publish], "success", true);
  // A changed bootloader set may have no board app to rebuild.
  verifyCoverage({ [cohort]: { bootloader: true, boards: [] } }, [set, setPublish], "success", true);

  const compileError = { ...app, conclusion: "failure",
    steps: [{ name: "Copy the board application from the compiled shard", conclusion: "failure" }] };
  verifyCoverage(expected, [set, setPublish, compileError], "failure", true);
  assert.throws(() => verifyCoverage(expected, [set, setPublish, { ...compileError, steps: [] }], "failure", true), /Missing/);
  assert.throws(() => verifyCoverage(expected, [set, setPublish, app, { ...publish, conclusion: "cancelled" }], "failure", true), /Incomplete/);
  // A failed bootloader set is accounted for; board apps still need their own results.
  const failedSet = { ...set, conclusion: "failure", steps: [{ name: "Compile the shared binaries", conclusion: "failure" }] };
  verifyCoverage(expected, [failedSet, app, publish], "failure", true);
  assert.throws(() => verifyCoverage(expected, [failedSet], "failure", true), /Missing/);
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
