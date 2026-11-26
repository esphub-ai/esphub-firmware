import { setTimeout as pause } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import {
  sdkHeaderEntry,
  readSdkVersion,
  replaceSdkVersion,
  sdkVersionPlan,
} from "./sdk-version.mjs";

const checkName = "SDK version";
const baseBranch = "main";
const workflows = ["firmware.yml"];

export function githubRequest(token, fetcher = fetch) {
  if (!token) throw Error("Missing GitHub token");
  return async (method, path, body) => {
    const response = await fetcher("https://api.github.com" + path, {
      method,
      headers: {
        Accept: "application/vnd.github+json",
        Authorization: "Bearer " + token,
        "X-GitHub-Api-Version": "2022-11-28",
        ...(body ? { "Content-Type": "application/json" } : {}),
      },
      ...(body ? { body: JSON.stringify(body) } : {}),
    });
    if (response.status === 204) return null;
    const result = await response.json();
    if (!response.ok)
      throw Error(
        `GitHub ${method} ${path}: ${response.status} ${result.message ?? "request failed"}`,
      );
    return result;
  };
}

function repositoryPath(repository) {
  if (!/^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repository))
    throw Error("Invalid GitHub repository");
  return "/repos/" + repository;
}

async function snapshot(request, root, sha) {
  const commit = await request("GET", `${root}/git/commits/${sha}`);
  const treeSha = commit.tree?.sha;
  if (
    typeof treeSha !== "string" ||
    /^[a-f0-9]{40}$/.exec(treeSha)?.[0] !== treeSha
  )
    throw Error("Invalid SDK tree SHA");
  // GET trees accepts a commit-ish but may echo that commit SHA in its receipt.
  // Resolve the actual tree first: POST trees.base_tree requires a tree object.
  const {
    tree,
    truncated,
    sha: returnedSha,
  } = await request("GET", `${root}/git/trees/${treeSha}?recursive=1`);
  if (truncated) throw Error("SDK tree response was truncated");
  if (returnedSha !== treeSha) throw Error("Unexpected SDK tree SHA");
  const header = sdkHeaderEntry(tree);
  const blob = await request("GET", `${root}/git/blobs/${header.sha}`);
  if (blob.encoding !== "base64" || blob.size > 1024 * 1024)
    throw Error("Invalid SDK header blob");
  return {
    files: tree,
    header: Buffer.from(blob.content, "base64").toString("utf8"),
    mode: header.mode,
    path: header.path,
  };
}

async function createCheck(request, root, sha) {
  return request("POST", `${root}/check-runs`, {
    name: checkName,
    head_sha: sha,
    status: "in_progress",
    output: {
      title: "Checking SDK version",
      summary:
        "Validating the SDK against main; only trusted base-branch code runs here.",
    },
  });
}

async function finishCheck(request, root, check, conclusion, summary) {
  await request("PATCH", `${root}/check-runs/${check.id}`, {
    status: "completed",
    conclusion,
    output: { title: checkName, summary },
  });
}

function releaseIntent(pull) {
  return pull.labels
    .map((label) => label.name)
    .filter((label) => label === "sdk:minor" || label === "sdk:major")
    .sort()
    .join(",");
}

async function confirmPr(request, root, pr) {
  const current = await request("GET", `${root}/pulls/${pr.number}`);
  if (
    current.state !== "open" ||
    current.head.sha !== pr.head.sha ||
    current.base.sha !== pr.base.sha ||
    current.base.ref !== baseBranch ||
    current.head.ref !== pr.head.ref ||
    releaseIntent(current) !== releaseIntent(pr)
  )
    throw Error(
      "PR head, base, or release labels changed during validation; rerun SDK version automation.",
    );
  return current;
}

async function writeVersion(request, root, pr, head, version) {
  if (head.mode !== "100644")
    throw Error(
      "Automatic SDK edits require a non-executable header; set the version manually.",
    );
  // expectedHeadOid is an atomic head lease. A non-forced REST ref update is
  // insufficient: it could silently restore commits after a contributor rewind.
  const result = await request("POST", "/graphql", {
    query: `mutation($input: CreateCommitOnBranchInput!) {
      createCommitOnBranch(input: $input) { commit { oid } }
    }`,
    variables: {
      input: {
        branch: {
          repositoryNameWithOwner: root.slice("/repos/".length),
          branchName: pr.head.ref,
        },
        expectedHeadOid: pr.head.sha,
        message: { headline: `chore: bump device SDK to ${version}` },
        fileChanges: {
          additions: [
            {
              path: head.path,
              contents: Buffer.from(
                replaceSdkVersion(head.header, version),
              ).toString("base64"),
            },
          ],
        },
      },
    },
  });
  if (result.errors?.length)
    throw Error(
      "GitHub SDK commit failed: " +
        result.errors.map((error) => error.message).join("; "),
    );
  const sha = result.data?.createCommitOnBranch?.commit?.oid;
  if (!sha) throw Error("GitHub SDK commit did not return a commit SHA");
  return sha;
}

async function ensureCi(request, root, pr) {
  for (const workflow of workflows) {
    const { workflow_runs: runs } = await request(
      "GET",
      `${root}/actions/workflows/${workflow}/runs?head_sha=${pr.head.sha}&per_page=100`,
    );
    let covered = false;
    for (const run of runs) {
      if (
        run.head_sha !== pr.head.sha ||
        run.head_branch !== pr.head.ref ||
        run.conclusion === "action_required"
      )
        continue;
      if (
        run.event === "pull_request" &&
        run.actor?.login === "github-actions[bot]"
      ) {
        // GitHub can create blocked/approval-only PR runs for native-token
        // commits. Zero-job records are not CI coverage, even if marked failed.
        const jobs = await request(
          "GET",
          `${root}/actions/runs/${run.id}/jobs?per_page=1`,
        );
        if (!jobs.total_count) continue;
      }
      covered = true;
      break;
    }
    // Accepted dispatches and actual failed/pending CI are not retried.
    if (covered) continue;
    await request("POST", `${root}/actions/workflows/${workflow}/dispatches`, {
      ref: pr.head.ref,
      inputs: { pr_number: String(pr.number) },
    });
  }
}

export async function reconcileSdkPullRequest({
  repository,
  number,
  request,
  wait = pause,
}) {
  const root = repositoryPath(repository);
  let pr = await request("GET", `${root}/pulls/${number}`);
  if (pr.state !== "open" || pr.base.ref !== baseBranch)
    return { number, action: "skipped" };
  const sameRepository =
    pr.head.repo?.id === pr.base.repo?.id &&
    pr.head.repo?.full_name?.toLowerCase() === repository.toLowerCase();
  if (sameRepository && pr.head.ref === baseBranch)
    throw Error("Refusing to update the default branch");
  let check = await createCheck(request, root, pr.head.sha);
  try {
    let head = await snapshot(request, root, pr.head.sha);
    let comparison = await request(
      "GET",
      `${root}/compare/${pr.base.sha}...${pr.head.sha}`,
    );
    const originalBase = await snapshot(
      request,
      root,
      comparison.merge_base_commit.sha,
    );
    const labels = pr.labels.map((label) => label.name);
    let plan = sdkVersionPlan({
      baseHeader: originalBase.header,
      headHeader: head.header,
      baseFiles: originalBase.files,
      headFiles: head.files,
      labels,
    });
    if (!plan.hasChanges) {
      await confirmPr(request, root, pr);
      await finishCheck(
        request,
        root,
        check,
        "success",
        `No SDK implementation changes. SDK ${plan.currentVersion}; no automatic bump is needed.`,
      );
      return { number, action: "unchanged", version: plan.currentVersion };
    }

    if (comparison.merge_base_commit.sha !== pr.base.sha) {
      if (!sameRepository)
        throw Error(
          "Update this fork branch to the latest main before checking the SDK version.",
        );
      const oldHead = pr.head.sha;
      await request("PUT", `${root}/pulls/${number}/update-branch`, {
        expected_head_sha: oldHead,
      });
      // update-branch is asynchronous. Wait for its one accepted operation;
      // never replay the write when its outcome is uncertain.
      let updated = false;
      for (let attempt = 0; attempt < 30; attempt++) {
        pr = await request("GET", `${root}/pulls/${number}`);
        if (pr.state !== "open")
          throw Error("PR closed while updating its branch");
        if (pr.head.sha !== oldHead) {
          updated = true;
          break;
        }
        await wait(1000);
      }
      if (!updated)
        throw Error(
          "Branch update is still pending; rerun SDK version automation after it finishes.",
        );
      await finishCheck(
        request,
        root,
        check,
        "neutral",
        "Superseded by the branch update.",
      );
      check = await createCheck(request, root, pr.head.sha);
      head = await snapshot(request, root, pr.head.sha);
      comparison = await request(
        "GET",
        `${root}/compare/${pr.base.sha}...${pr.head.sha}`,
      );
      if (comparison.merge_base_commit.sha !== pr.base.sha)
        throw Error(
          "main changed during the branch update; rerun SDK version automation.",
        );
    }

    const base = await snapshot(request, root, pr.base.sha);
    plan = sdkVersionPlan({
      baseHeader: base.header,
      headHeader: head.header,
      baseFiles: base.files,
      headFiles: head.files,
      labels: pr.labels.map((label) => label.name),
    });
    if (plan.needsUpdate) {
      if (!sameRepository)
        throw Error(
          `Set the SDK version to ${plan.expectedVersion} in this fork; fork branches are never written or dispatched.`,
        );
      const sha = await writeVersion(
        request,
        root,
        pr,
        head,
        plan.expectedVersion,
      );
      await finishCheck(
        request,
        root,
        check,
        "neutral",
        "Superseded by the SDK version commit.",
      );
      check = await createCheck(request, root, sha);
      let updatedPr;
      for (let attempt = 0; attempt < 30; attempt++) {
        updatedPr = await request("GET", `${root}/pulls/${number}`);
        if (
          updatedPr.state !== "open" ||
          updatedPr.base.sha !== pr.base.sha ||
          releaseIntent(updatedPr) !== releaseIntent(pr)
        )
          throw Error(
            "PR base or release intent changed after the SDK commit; check again.",
          );
        if (updatedPr.head.sha === sha) break;
        if (updatedPr.head.sha !== pr.head.sha)
          throw Error(
            `PR head changed after the SDK commit: expected ${sha}, observed ${updatedPr.head.sha}.`,
          );
        // A successful GraphQL commit can precede REST PR-cache propagation.
        // Wait only on the inspected old head; never replay the accepted write.
        if (attempt === 29)
          throw Error(
            `SDK commit ${sha} is not yet visible in the PR; rerun after it propagates.`,
          );
        await wait(1000);
      }
      pr = updatedPr;
      head = await snapshot(request, root, sha);
    }

    // Re-read main after any writes, so stale results never get a success check.
    const latestBase = await request(
      "GET",
      `${root}/git/ref/heads/${baseBranch}`,
    );
    if (latestBase.object.sha !== pr.base.sha)
      throw Error(
        "main changed while assigning the SDK version; rerun SDK version automation.",
      );
    if (readSdkVersion(head.header) !== plan.expectedVersion)
      throw Error(
        "SDK version did not match the assigned version after the write.",
      );
    await confirmPr(request, root, pr);
    if (sameRepository) await ensureCi(request, root, pr);
    await confirmPr(request, root, pr);
    await finishCheck(
      request,
      root,
      check,
      "success",
      `SDK ${plan.expectedVersion} matches main ${pr.base.sha} at head ${pr.head.sha}. Board builds are advisory on pull requests.`,
    );
    return {
      number,
      action: "checked",
      version: plan.expectedVersion,
      head: pr.head.sha,
    };
  } catch (error) {
    await finishCheck(request, root, check, "failure", error.message);
    throw error;
  }
}

export async function reconcileSdkVersions({ repository, request, wait }) {
  const root = repositoryPath(repository);
  const pulls = [];
  for (let page = 1; ; page++) {
    const batch = await request(
      "GET",
      `${root}/pulls?state=open&base=${baseBranch}&per_page=100&page=${page}`,
    );
    pulls.push(...batch);
    if (batch.length < 100) break;
  }
  const errors = [];
  for (const { number } of pulls) {
    try {
      const result = await reconcileSdkPullRequest({
        repository,
        number,
        request,
        wait,
      });
      console.log(JSON.stringify(result));
    } catch (error) {
      errors.push(`PR #${number}: ${error.message}`);
    }
  }
  if (errors.length) throw Error(errors.join("\n"));
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  await reconcileSdkVersions({
    repository: process.env.GITHUB_REPOSITORY,
    request: githubRequest(process.env.GH_TOKEN),
  });
}
