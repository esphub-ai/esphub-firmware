import { execFileSync, spawnSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const branch = "ci/firmware-coverage";
const remoteRef = "refs/heads/" + branch;

export function coverageCommit(cwd = process.cwd()) {
  const tracking = "refs/remotes/origin/" + branch;
  const refs = execFileSync("git", ["for-each-ref", "--format=%(refname) %(objectname)", tracking],
    { cwd, encoding: "utf8" });
  return refs.split("\n").find(line => line.startsWith(tracking + " "))?.split(" ")[1] ?? "";
}

export function isAncestor(before, after, cwd = process.cwd()) {
  const result = spawnSync("git", ["merge-base", "--is-ancestor", before, after], { cwd, encoding: "utf8" });
  if (result.status !== 0 && result.status !== 1) throw Error(result.stderr || "Cannot compare firmware revisions");
  return result.status === 0;
}

// Main batches compile apps in shards; the board job then copies its app.
const buildSteps = {
  app: "Copy the board application from the compiled shard",
  bootloader: "Assemble bootloader release for the board",
};

function attempted(job, step) {
  return job?.conclusion === "success" || (job?.conclusion === "failure" &&
    job.steps?.some(s => s.name === step && ["success", "failure"].includes(s.conclusion)));
}

export function verifyCoverage(expected, jobs, buildResult, publishEnabled) {
  const groups = Object.entries(expected);
  if (!groups.length) {
    if (buildResult !== "skipped") throw Error("Incomplete empty board matrix: " + buildResult);
    return;
  }
  if (!["success", "failure"].includes(buildResult)) throw Error("Incomplete board matrix: " + buildResult);
  const byName = new Map(jobs.map(job => [job.name, job]));

  for (const [cohort, boards] of groups) {
    const prefix = cohort + " / ";
    // Also catch an interrupted USB kit or publication dispatch, including
    // cancellation in a matrix where a different board had a compiler error.
    const interrupted = jobs.find(job => job.name.startsWith(prefix) &&
      !["success", "failure", "skipped"].includes(job.conclusion));
    if (interrupted) throw Error("Incomplete firmware job: " + interrupted.name);

    const shared = byName.get(prefix + "bootloader / build");
    if (!attempted(shared, "Compile the shared binaries")) throw Error("Missing shared bootloader result: " + cohort);
    if (shared.conclusion === "failure") continue; // The prerequisite itself was built and failed.
    for (const [board, kinds] of boards) {
      const boardPrefix = prefix + board + " / ";
      let failed = false;
      for (const kind of kinds.filter(kind => kind !== "setup")) {
        const job = byName.get(boardPrefix + kind);
        if (!attempted(job, buildSteps[kind])) throw Error("Missing firmware build result: " + boardPrefix + kind);
        if (job.conclusion === "failure") failed = true;
      }
      if (!failed && kinds.includes("setup")) {
        const kit = byName.get(boardPrefix + "USB kit");
        if (!attempted(kit, "Run ./.github/actions/build-kit")) throw Error("Missing USB kit result: " + board);
        failed = kit.conclusion === "failure";
      }
      if (!failed && publishEnabled &&
          !attempted(byName.get(boardPrefix + "queue-publish"), "Dispatch this board for publication")) {
        throw Error("Missing publication dispatch result: " + board);
      }
    }
  }
}

export function advanceCoverage(sha, plannedBase, cwd = process.cwd()) {
  // Main runs can finish out of order. Compare-and-swap only the CI branch,
  // and re-read both refs if a concurrent writer wins the first push.
  for (let attempt = 0; attempt < 2; attempt++) {
    execFileSync("git", ["fetch", "--quiet", "--no-tags", "--prune", "origin",
      "+refs/heads/*:refs/remotes/origin/*"], { cwd });
    const main = "refs/remotes/origin/main";
    const covered = coverageCommit(cwd);
    if (!isAncestor(sha, main, cwd)) return; // Superseded by a main history rewrite.
    if (covered && isAncestor(covered, main, cwd) && isAncestor(sha, covered, cwd)) return;
    if (covered !== plannedBase && (!covered || !isAncestor(covered, sha, cwd) ||
        (plannedBase && !isAncestor(plannedBase, covered, cwd)))) {
      // A reset during the build changed the range that still needs checking.
      return;
    }
    const result = spawnSync("git", ["push", "--force-with-lease=" + remoteRef + ":" + covered,
      "origin", sha + ":" + remoteRef], { cwd, encoding: "utf8" });
    if (result.status === 0) return;
    if (attempt === 1) throw Error(result.stderr || "Could not record main firmware coverage");
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const expected = JSON.parse(process.env.EXPECTED_BUILDS);
  let jobs = [];
  if (Object.keys(expected).length) {
    const endpoint = `repos/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}/jobs?filter=latest&per_page=100`;
    const pages = execFileSync("gh", ["api", "--paginate", "--slurp", endpoint],
      { encoding: "utf8", maxBuffer: 32 * 1024 * 1024 });
    jobs = JSON.parse(pages).flatMap(page => page.jobs);
  }
  verifyCoverage(expected, jobs, process.env.BUILD_RESULT, process.env.PUBLISH_ENABLED === "true");
  advanceCoverage(process.env.GITHUB_SHA, process.env.COVERAGE_BASE);
}
