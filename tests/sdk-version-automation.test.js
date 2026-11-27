import assert from "node:assert/strict";
import test from "node:test";
import {
  githubRequest,
  reconcileSdkPullRequest,
  reconcileSdkVersions,
} from "../scripts/sdk-version-automation.mjs";
import { SDK_HEADER, SDK_ROOT } from "../scripts/sdk-version.mjs";

const repository = "esphub-ai/esphub-firmware";
const root = `/repos/${repository}`;
const treeShas = Object.fromEntries(
  ["base", "head", "ancestor", "merged", "sdk", "forward", "divergent"].map(
    (name, i) => [name, String(i + 1).repeat(40)],
  ),
);
const header = (version) =>
  `#pragma once\n#define EH_SDK_VERSION "${version}" // keep this comment\nint eh_agent_start(void);\n`;
const clone = (value) => structuredClone(value);
const immediate = async () => {};
const commitMutation =
  "mutation($input:CreateCommitOnBranchInput!){createCommitOnBranch(input:$input){commit{oid}}}";

// These are in-memory GitHub API results, including modeled native-token writes.
// No test invokes GitHub, git, a workflow, or a real timer.
function fixture({
  version = "1.2.3",
  code = "changed",
  fork = false,
  labels = [],
  before,
  graphqlResponse,
  staleCommitReads = 0,
} = {}) {
  const calls = [];
  const snapshots = new Map();
  const blobs = new Map();
  const state = {
    pr: {
      number: 7,
      state: "open",
      base: {
        ref: "main",
        sha: "base",
        repo: { id: 100, full_name: repository },
      },
      head: {
        sha: "head",
        ref: "feature/sdk",
        repo: {
          id: fork ? 200 : 100,
          full_name: fork ? "contributor/esphub" : repository,
        },
      },
      labels: labels.map((name) => ({ name })),
    },
    mergeBase: "base",
    latestBase: "base",
    runs: {},
    runJobs: {},
    checks: [],
    prReadQueue: [],
  };
  function snapshot(
    sha,
    {
      version = "1.2.3",
      code = "changed",
      content = header(version),
      files = [],
      mode = "100644",
    } = {},
  ) {
    const entry = {
      path: SDK_HEADER,
      mode,
      type: "blob",
      sha: `${sha}-header`,
    };
    snapshots.set(sha, {
      sha: treeShas[sha],
      truncated: false,
      tree: [
        entry,
        { path: `${SDK_ROOT}agent.c`, mode: "100644", type: "blob", sha: code },
        ...files,
      ],
    });
    blobs.set(entry.sha, {
      encoding: "base64",
      size: Buffer.byteLength(content),
      content: Buffer.from(content).toString("base64"),
    });
  }
  snapshot("base", { code: "original" });
  snapshot("head", { version, code });
  const request = async (method, path, body) => {
    const call = { method, path, body: clone(body) };
    calls.push(call);
    await before?.(call, state);
    if (method === "GET" && path === `${root}/pulls/7`)
      return clone(state.prReadQueue.shift() ?? state.pr);
    const commitSha = path.match(/\/git\/commits\/([^/]+)$/)?.[1];
    if (method === "GET" && commitSha) {
      assert.ok(snapshots.has(commitSha), `Unknown commit ${commitSha}`);
      return { tree: { sha: snapshots.get(commitSha).sha } };
    }
    const treeSha = path.match(/\/git\/trees\/([^?]+)\?recursive=1$/)?.[1];
    if (method === "GET" && treeSha) {
      const snapshot = [...snapshots.values()].find(
        (value) => value.sha === treeSha,
      );
      assert.ok(snapshot, `Unknown tree ${treeSha}`);
      return clone(snapshot);
    }
    const blobSha = path.match(/\/git\/blobs\/(.+)$/)?.[1];
    if (method === "GET" && blobSha) return clone(blobs.get(blobSha));
    if (
      method === "GET" &&
      path === `${root}/compare/${state.pr.base.sha}...${state.pr.head.sha}`
    )
      return { merge_base_commit: { sha: state.mergeBase } };
    if (method === "POST" && path === `${root}/check-runs`) {
      const check = { id: state.checks.length + 1, ...clone(body) };
      state.checks.push(check);
      return { id: check.id };
    }
    if (method === "PATCH" && path.includes("/check-runs/")) {
      Object.assign(
        state.checks.find(({ id }) => path.endsWith(`/${id}`)),
        clone(body),
      );
      return {};
    }
    if (method === "PUT" && path === `${root}/pulls/7/update-branch`) {
      state.updating = true;
      return { message: "Updating pull request branch", url: "fixture" };
    }
    if (method === "POST" && path === "/graphql") {
      assert.equal(body.query.replace(/\s/g, ""), commitMutation);
      const { input } = body.variables;
      assert.deepEqual(input.branch, {
        repositoryNameWithOwner: repository,
        branchName: state.pr.head.ref,
      });
      // Unlike REST force:false, the lease rejects ancestor rewinds as well as
      // forward/divergent moves, before creating a commit or changing the branch.
      if (state.pr.head.sha !== input.expectedHeadOid)
        return {
          errors: [
            { message: "Concurrent head changed: expectedHeadOid mismatch" },
          ],
        };
      if (graphqlResponse !== undefined) return clone(graphqlResponse);
      assert.deepEqual(Object.keys(input.fileChanges), ["additions"]);
      assert.equal(input.fileChanges.additions.length, 1);
      const [{ path: changedPath, contents }] = input.fileChanges.additions;
      assert.equal(changedPath, SDK_HEADER);
      const old = snapshots.get(input.expectedHeadOid);
      assert.equal(
        old.tree.find(({ path }) => path === SDK_HEADER).mode,
        "100644",
      );
      snapshots.set("sdk-commit", {
        ...clone(old),
        sha: treeShas.sdk,
        tree: old.tree.map((entry) =>
          entry.path === SDK_HEADER
            ? { ...entry, sha: "sdk-header" }
            : clone(entry),
        ),
      });
      blobs.set("sdk-header", {
        encoding: "base64",
        size: Buffer.from(contents, "base64").length,
        content: contents,
      });
      state.prReadQueue = Array.from({ length: staleCommitReads }, () =>
        clone(state.pr),
      );
      state.pr.head.sha = "sdk-commit";
      return {
        data: { createCommitOnBranch: { commit: { oid: "sdk-commit" } } },
      };
    }
    if (method === "GET" && path === `${root}/git/ref/heads/main`)
      return { object: { sha: state.latestBase } };
    const runId = path.match(/\/actions\/runs\/(\d+)\/jobs\?per_page=1$/)?.[1];
    if (method === "GET" && runId)
      return { total_count: state.runJobs[runId] ?? 0 };
    const workflow = path.match(/\/actions\/workflows\/([^/]+)\//)?.[1];
    if (method === "GET" && workflow)
      return { workflow_runs: clone(state.runs[workflow] ?? []) };
    if (method === "POST" && workflow && path.endsWith("/dispatches"))
      return null;
    throw Error(`Unexpected fixture request: ${method} ${path}`);
  };
  const run = (options = {}) =>
    reconcileSdkPullRequest({
      repository,
      number: 7,
      request,
      wait: immediate,
      ...options,
    });
  const branchWrites = () =>
    calls.filter(
      ({ method, path }) =>
        (method === "POST" && path === "/graphql") ||
        (method === "PUT" && path.endsWith("/update-branch")),
    );
  const dispatches = () =>
    calls.filter(
      ({ method, path }) => method === "POST" && path.endsWith("/dispatches"),
    );
  return {
    calls,
    snapshots,
    state,
    snapshot,
    request,
    run,
    branchWrites,
    dispatches,
  };
}

function failed(f) {
  assert.equal(f.state.checks.at(-1).conclusion, "failure");
  assert.ok(f.state.checks.every(({ conclusion }) => conclusion !== "success"));
}
function noBranchActivity(f) {
  assert.deepEqual(f.branchWrites(), []);
  assert.deepEqual(f.dispatches(), []);
}

test("githubRequest sends API JSON and bearer headers, and accepts 204 without parsing JSON", async () => {
  const calls = [];
  const request = githubRequest("fixture-token", async (...args) => {
    calls.push(args);
    return calls.length === 3
      ? { status: 204, json: () => assert.fail("204 has no JSON body") }
      : { status: 200, ok: true, json: async () => ({ id: calls.length }) };
  });
  assert.deepEqual(await request("GET", "/repos/a/b"), { id: 1 });
  assert.deepEqual(
    await request("POST", "/graphql", { query: commitMutation, variables: {} }),
    { id: 2 },
  );
  assert.equal(
    await request("POST", "/repos/a/b/actions/workflows/ci.yml/dispatches", {
      ref: "work",
    }),
    null,
  );
  assert.equal(calls[0][0], "https://api.github.com/repos/a/b");
  assert.deepEqual(calls[0][1], {
    method: "GET",
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: "Bearer fixture-token",
      "X-GitHub-Api-Version": "2022-11-28",
    },
  });
  assert.equal(calls[1][1].headers["Content-Type"], "application/json");
  assert.equal(calls[1][0], "https://api.github.com/graphql");
  assert.deepEqual(JSON.parse(calls[1][1].body), {
    query: commitMutation,
    variables: {},
  });
  assert.throws(
    () => githubRequest("", () => assert.fail("missing token must not fetch")),
    /token/i,
  );
});

test("githubRequest never retries HTTP, transport, or JSON errors", async (t) => {
  for (const status of [403, 429, 503]) {
    await t.test(`HTTP ${status}`, async () => {
      let calls = 0;
      const request = githubRequest("fixture-token", async () => {
        calls++;
        return { status, ok: false, json: async () => ({ message: "denied" }) };
      });
      await assert.rejects(
        request("POST", "/graphql", { query: commitMutation, variables: {} }),
        new RegExp(`${status} denied`),
      );
      assert.equal(calls, 1);
    });
  }
  for (const phase of ["transport", "json"]) {
    let calls = 0;
    const request = githubRequest("fixture-token", async () => {
      calls++;
      if (phase === "transport") throw Error(phase);
      return {
        status: 200,
        ok: true,
        json: async () => {
          throw Error(phase);
        },
      };
    });
    await assert.rejects(request("POST", "/fixture", {}), new RegExp(phase));
    assert.equal(calls, 1);
  }
});

test("repository names are validated before either reconciler makes a request", async () => {
  for (const repository of [
    undefined,
    null,
    "",
    "owner",
    "a/b/c",
    "a/b?token=x",
    "a/../b",
    "a/b\n",
  ]) {
    let calls = 0;
    const request = async () => {
      calls++;
      throw Error("Unexpected request");
    };
    await assert.rejects(
      reconcileSdkPullRequest({
        repository,
        number: 7,
        request,
        wait: immediate,
      }),
      /repository/i,
    );
    await assert.rejects(
      reconcileSdkVersions({ repository, request, wait: immediate }),
      /repository/i,
    );
    assert.equal(calls, 0);
  }
});

test("same-repository SDK changes write one header on the inspected HEAD tree and dispatch both workflows", async (t) => {
  for (const [labels, version] of [
    [[], "1.2.4"],
    [["sdk:minor"], "1.3.0"],
    [["sdk:major"], "2.0.0"],
  ]) {
    await t.test(labels[0] ?? "default patch", async () => {
      const f = fixture({ labels });
      const otherFiles = [
        { path: "web/app.js", mode: "100644", type: "blob", sha: "web-blob" },
        {
          path: `${SDK_ROOT}tool.sh`,
          mode: "100755",
          type: "blob",
          sha: "exec-blob",
        },
      ];
      const content = header("1.2.3").replace(
        "keep this comment",
        "保留注释 — café",
      );
      f.snapshot("head", { content, files: otherFiles });
      const inspectedTree = clone(f.snapshots.get("head").tree);
      f.state.pr.head.repo.full_name = repository.toUpperCase();
      assert.deepEqual(await f.run(), {
        number: 7,
        action: "checked",
        version,
        head: "sdk-commit",
      });
      assert.equal(f.branchWrites().length, 1);
      const [write] = f.branchWrites();
      assert.deepEqual(
        {
          ...write,
          body: { ...write.body, query: write.body.query.replace(/\s/g, "") },
        },
        {
          method: "POST",
          path: "/graphql",
          body: {
            query: commitMutation,
            variables: {
              input: {
                branch: {
                  repositoryNameWithOwner: repository,
                  branchName: "feature/sdk",
                },
                expectedHeadOid: "head",
                message: { headline: `chore: bump device SDK to ${version}` },
                fileChanges: {
                  additions: [
                    {
                      path: SDK_HEADER,
                      contents: Buffer.from(
                        content.replace('"1.2.3"', `"${version}"`),
                        "utf8",
                      ).toString("base64"),
                    },
                  ],
                },
              },
            },
          },
        },
      );
      assert.deepEqual(
        f.snapshots.get("sdk-commit").tree,
        inspectedTree.map((entry) =>
          entry.path === SDK_HEADER ? { ...entry, sha: "sdk-header" } : entry,
        ),
      );
      assert.deepEqual(f.snapshots.get("head").tree, inspectedTree);
      for (const sha of ["base", "head"]) {
        const commitIndex = f.calls.findIndex(
          ({ method, path }) =>
            method === "GET" && path === `${root}/git/commits/${sha}`,
        );
        const treeIndex = f.calls.findIndex(
          ({ method, path }) =>
            method === "GET" &&
            path === `${root}/git/trees/${treeShas[sha]}?recursive=1`,
        );
        assert.ok(
          commitIndex >= 0 && treeIndex > commitIndex,
          "resolve commit to actual tree SHA before reading snapshot",
        );
      }
      assert.equal(
        f.calls.some(
          ({ method, path }) =>
            method !== "GET" && /\/git\/(trees|commits|refs)/.test(path),
        ),
        false,
      );
      assert.deepEqual(
        f.dispatches().map(({ path, body }) => [path, body]),
        ["firmware.yml"].map((workflow) => [
          `${root}/actions/workflows/${workflow}/dispatches`,
          { ref: "feature/sdk", inputs: { pr_number: "7" } },
        ]),
      );
      assert.deepEqual(
        f.state.checks.map(({ head_sha, conclusion }) => [
          head_sha,
          conclusion,
        ]),
        [
          ["head", "neutral"],
          ["sdk-commit", "success"],
        ],
      );
      assert.ok(
        f.calls
          .filter(({ path }) => path.includes("/runs?"))
          .every(({ path }) => path.includes("head_sha=sdk-commit")),
      );
    });
  }
});

test("blocked or zero-job bot PR runs do not suppress explicit CI dispatch", async (t) => {
  for (const conclusion of ["action_required", "failure", null]) {
    await t.test(String(conclusion), async () => {
      const f = fixture({ version: "1.2.4" });
      for (const workflow of ["firmware.yml"])
        f.state.runs[workflow] = [
          {
            id: 123,
            head_sha: "head",
            head_branch: "feature/sdk",
            event: "pull_request",
            actor: { login: "github-actions[bot]" },
            conclusion,
          },
        ];
      await f.run();
      assert.equal(f.dispatches().length, 1);
      assert.equal(f.branchWrites().length, 0);
    });
  }
});

test("failed bot PR CI with real jobs is not automatically retried", async () => {
  const f = fixture({ version: "1.2.4" });
  f.state.runJobs[123] = 1;
  for (const workflow of ["firmware.yml"])
    f.state.runs[workflow] = [
      {
        id: 123,
        head_sha: "head",
        head_branch: "feature/sdk",
        event: "pull_request",
        actor: { login: "github-actions[bot]" },
        conclusion: "failure",
      },
    ];
  await f.run();
  noBranchActivity(f);
});

test("accepted commits wait for stale REST PR snapshots without another write", async () => {
  const f = fixture({ staleCommitReads: 2 });
  const waits = [];
  const result = await f.run({ wait: async (ms) => waits.push(ms) });
  assert.equal(result.head, "sdk-commit");
  assert.deepEqual(waits, [1000, 1000]);
  assert.equal(f.branchWrites().length, 1);
  assert.equal(f.dispatches().length, 1);
  assert.equal(f.state.checks.at(-1).conclusion, "success");
});

test("a commit that remains absent from REST fails within a bounded readback", async () => {
  const f = fixture({ staleCommitReads: 30 });
  let waits = 0;
  await assert.rejects(f.run({ wait: async () => waits++ }), /not yet visible/);
  assert.equal(waits, 29);
  assert.equal(f.state.pr.head.sha, "sdk-commit");
  assert.equal(f.branchWrites().length, 1);
  assert.equal(f.dispatches().length, 0);
  failed(f);
});

test("exact tree arrays detect SDK deletion, addition, and mode changes", async (t) => {
  for (const kind of ["deletion", "addition", "mode"]) {
    await t.test(kind, async () => {
      const f = fixture({ code: "original" });
      const entry = {
        path: `${SDK_ROOT}extra.c`,
        mode: "100644",
        type: "blob",
        sha: "extra",
      };
      if (kind === "deletion") f.snapshots.get("base").tree.push(entry);
      if (kind === "addition") f.snapshots.get("head").tree.push(entry);
      if (kind === "mode") f.snapshots.get("head").tree[1].mode = "100755";
      assert.equal((await f.run()).version, "1.2.4");
      assert.equal(
        f.branchWrites()[0].body.variables.input.expectedHeadOid,
        "head",
      );
    });
  }
});

test("matching workflow SHA and branch prevent duplicate dispatch, including failed runs", async (t) => {
  for (const conclusion of ["failure", "success", null]) {
    await t.test(String(conclusion), async () => {
      const f = fixture({ version: "1.2.4" });
      for (const workflow of ["firmware.yml"]) {
        f.state.runs[workflow] = [
          { head_sha: "head", head_branch: "feature/sdk", conclusion },
        ];
      }
      assert.equal((await f.run()).action, "checked");
      noBranchActivity(f);
    });
  }
  const f = fixture({ version: "1.2.4" });
  f.state.runs["firmware.yml"] = [
    { head_sha: "old-head", head_branch: "feature/sdk" },
    { head_sha: "head", head_branch: "other-branch" },
  ];
  await f.run();
  assert.equal(
    f.dispatches().length,
    1,
    "neither SHA-only nor branch-only matches are sufficient",
  );
});

test("no implementation changes means no branch writes or workflow dispatch", async (t) => {
  for (const kind of [
    "identical",
    "outside SDK",
    "docs and tests",
    "version literal only",
  ]) {
    await t.test(kind, async () => {
      const f = fixture({
        code: "original",
        version: kind === "version literal only" ? "1.2.4" : "1.2.3",
      });
      const paths =
        kind === "outside SDK"
          ? ["web/app.js"]
          : kind === "docs and tests"
            ? [
                `${SDK_ROOT}README.md`,
                `${SDK_ROOT}docs/guide.txt`,
                `${SDK_ROOT}tests/agent.c`,
              ]
            : [];
      f.snapshots.get("head").tree.push(
        ...paths.map((path) => ({
          path,
          mode: "100644",
          type: "blob",
          sha: "changed",
        })),
      );
      assert.equal((await f.run()).action, "unchanged");
      assert.equal(f.state.checks.at(-1).conclusion, "success");
      noBranchActivity(f);
    });
  }
});

test("fork versions are checked without ever writing or dispatching the fork", async (t) => {
  for (const correct of [false, true]) {
    await t.test(correct ? "correct" : "wrong", async () => {
      const f = fixture({ fork: true, version: correct ? "1.2.4" : "1.2.3" });
      if (correct) {
        assert.equal((await f.run()).version, "1.2.4");
        assert.equal(f.state.checks.at(-1).conclusion, "success");
      } else {
        await assert.rejects(f.run(), /1\.2\.4.*fork/i);
        failed(f);
      }
      noBranchActivity(f);
    });
  }
});

test("a matching repository name with a different ID is still an unwritable fork", async () => {
  const f = fixture({ fork: true });
  f.state.pr.head.repo.full_name = repository;
  await assert.rejects(f.run(), /fork/i);
  noBranchActivity(f);
  failed(f);
});

function behind(options = {}) {
  const f = fixture(options);
  f.state.mergeBase = "ancestor";
  f.snapshot("ancestor", { version: "1.2.2", code: "original" });
  f.snapshot("base", { version: "1.4.8", code: "new-main" });
  f.snapshot("merged", { version: "1.4.8", code: "changed" });
  return f;
}

test("behind SDK PR updates once, awaits the refreshed head, and recomputes against new main", async () => {
  let polls = 0;
  const waits = [];
  const f = behind({
    before(call, state) {
      if (call.path === `${root}/pulls/7` && state.updating && ++polls === 3) {
        state.pr.head.sha = "merged";
        state.mergeBase = "base";
      }
    },
  });
  assert.deepEqual(
    await f.run({
      wait: async (ms) => {
        waits.push(ms);
      },
    }),
    { number: 7, action: "checked", version: "1.4.9", head: "sdk-commit" },
  );
  assert.deepEqual(
    f.calls.filter(({ method }) => method === "PUT"),
    [
      {
        method: "PUT",
        path: `${root}/pulls/7/update-branch`,
        body: { expected_head_sha: "head" },
      },
    ],
  );
  assert.deepEqual(waits, [1000, 1000]);
  assert.equal(f.branchWrites().at(-1).path, "/graphql");
  assert.equal(
    f.branchWrites().at(-1).body.variables.input.expectedHeadOid,
    "merged",
  );
  assert.deepEqual(
    f.state.checks.map(({ head_sha, conclusion }) => [head_sha, conclusion]),
    [
      ["head", "neutral"],
      ["merged", "neutral"],
      ["sdk-commit", "success"],
    ],
  );
});

test("pending update-branch is never replayed and an outdated fork is never updated", async () => {
  const pending = behind();
  await assert.rejects(pending.run(), /pending/i);
  assert.equal(pending.branchWrites().length, 1);
  assert.equal(pending.branchWrites()[0].method, "PUT");
  assert.equal(pending.dispatches().length, 0);
  failed(pending);
  const fork = behind({ fork: true });
  await assert.rejects(fork.run(), /fork.*latest main/i);
  noBranchActivity(fork);
  failed(fork);
});

test("concurrent head and main changes reject instead of recording a successful check", async (t) => {
  for (const race of [
    "head before atomic commit",
    "head after atomic commit",
    "main",
    "main during update",
  ]) {
    await t.test(race, async () => {
      const options = {
        before(call, state) {
          if (
            race === "head before atomic commit" &&
            call.method === "POST" &&
            call.path === "/graphql"
          )
            state.pr.head.sha = "contributor-head";
          if (
            race === "head after atomic commit" &&
            call.path === `${root}/pulls/7` &&
            state.pr.head.sha === "sdk-commit"
          )
            state.pr.head.sha = "contributor-head";
          if (race === "main" && call.path === `${root}/git/ref/heads/main`)
            state.latestBase = "new-main";
          if (
            race === "main during update" &&
            call.path === `${root}/pulls/7` &&
            state.updating
          )
            state.pr.head.sha = "merged";
        },
      };
      const f =
        race === "main during update" ? behind(options) : fixture(options);
      await assert.rejects(f.run(), /concurrent|changed|main/i);
      failed(f);
      assert.equal(f.dispatches().length, 0);
      if (race === "main during update")
        assert.equal(f.branchWrites().length, 1);
    });
  }
});

test("atomic expectedHeadOid rejects ancestor rewinds, forward moves, and divergent heads without retry", async (t) => {
  for (const [name, movedHead] of [
    [
      "ancestor rewind (REST force:false would accept restoring the old head)",
      "base",
    ],
    ["concurrent forward commit", "forward"],
    ["concurrent divergent commit", "divergent"],
  ]) {
    await t.test(name, async () => {
      const f = fixture({
        before(call, state) {
          if (call.method === "POST" && call.path === "/graphql")
            state.pr.head.sha = movedHead;
        },
      });
      if (movedHead !== "base") f.snapshot(movedHead, { code: movedHead });
      const original = clone(f.snapshots.get(movedHead));
      await assert.rejects(
        f.run(),
        /expectedHeadOid|concurrent|head.*changed/i,
      );
      assert.equal(
        f.state.pr.head.sha,
        movedHead,
        "never restore or overwrite the contributor's head",
      );
      assert.deepEqual(f.snapshots.get(movedHead), original);
      assert.equal(f.snapshots.has("sdk-commit"), false);
      assert.equal(f.branchWrites().length, 1);
      assert.equal(
        f.branchWrites()[0].body.variables.input.expectedHeadOid,
        "head",
      );
      assert.equal(f.dispatches().length, 0);
      failed(f);
    });
  }
});

test("GraphQL HTTP 200 errors and missing commit receipts fail without retry or dispatch", async (t) => {
  for (const [name, graphqlResponse, message] of [
    ["errors", { errors: [{ message: "lease rejected" }] }, /lease rejected/],
    [
      "partial data with errors",
      {
        data: { createCommitOnBranch: { commit: { oid: "uncertain" } } },
        errors: [{ message: "partial mutation error" }],
      },
      /partial mutation error/,
    ],
    ["missing data", {}, /commit/i],
    ["null mutation", { data: { createCommitOnBranch: null } }, /commit/i],
    ["missing commit", { data: { createCommitOnBranch: {} } }, /commit/i],
    [
      "null commit",
      { data: { createCommitOnBranch: { commit: null } } },
      /commit/i,
    ],
    [
      "missing oid",
      { data: { createCommitOnBranch: { commit: {} } } },
      /commit/i,
    ],
  ]) {
    await t.test(name, async () => {
      const f = fixture({ graphqlResponse });
      // Exercise the actual HTTP adapter: GraphQL failures use HTTP 200, not
      // the REST non-2xx error path, and must be checked by writeVersion.
      const httpCalls = [];
      const request = githubRequest("fixture-token", async (url, options) => {
        httpCalls.push({ url, method: options.method });
        const body = options.body ? JSON.parse(options.body) : undefined;
        const result = await f.request(
          options.method,
          url.slice("https://api.github.com".length),
          body,
        );
        return { status: 200, ok: true, json: async () => result };
      });
      await assert.rejects(f.run({ request }), message);
      assert.equal(
        httpCalls.filter(({ url }) => url.endsWith("/graphql")).length,
        1,
      );
      assert.equal(f.branchWrites().length, 1);
      assert.equal(f.state.pr.head.sha, "head");
      assert.equal(f.snapshots.has("sdk-commit"), false);
      assert.equal(f.dispatches().length, 0);
      failed(f);
    });
  }
});

test("executable headers require a manual version edit rather than losing the executable bit", async (t) => {
  for (const correct of [false, true]) {
    await t.test(
      correct ? "manually correct: no write" : "automatic edit refused",
      async () => {
        const f = fixture({ version: correct ? "1.2.4" : "1.2.3" });
        f.snapshots.get("head").tree[0].mode = "100755";
        if (correct) {
          assert.equal((await f.run()).version, "1.2.4");
          assert.equal(f.state.checks.at(-1).conclusion, "success");
        } else {
          await assert.rejects(f.run(), /executable|100644|mode|manual/i);
          failed(f);
          assert.equal(f.dispatches().length, 0);
        }
        assert.deepEqual(f.branchWrites(), []);
        assert.equal(f.snapshots.get("head").tree[0].mode, "100755");
      },
    );
  }
});

test("final PR refresh rejects a moved head or changed release intent before success or dispatch", async (t) => {
  for (const [name, options, mutate] of [
    [
      "already-correct head moved",
      { version: "1.2.4" },
      (pr) => {
        pr.head.sha = "contributor-head";
      },
    ],
    [
      "major label added",
      { version: "1.2.4" },
      (pr) => {
        pr.labels = [{ name: "sdk:major" }];
      },
    ],
    [
      "base moved",
      { version: "1.2.4" },
      (pr) => {
        pr.base.sha = "new-main";
      },
    ],
    [
      "branch renamed",
      { version: "1.2.4" },
      (pr) => {
        pr.head.ref = "other-branch";
      },
    ],
    [
      "minor label removed",
      { version: "1.3.0", labels: ["sdk:minor"] },
      (pr) => {
        pr.labels = [];
      },
    ],
    [
      "label changed after SDK commit",
      {},
      (pr) => {
        pr.labels = [{ name: "sdk:minor" }];
      },
    ],
    [
      "unchanged SDK head moved",
      { code: "original" },
      (pr) => {
        pr.head.sha = "contributor-head";
      },
    ],
  ]) {
    await t.test(name, async () => {
      let reads = 0;
      const f = fixture({
        ...options,
        before(call, state) {
          if (call.path === `${root}/pulls/7` && ++reads > 1) mutate(state.pr);
        },
      });
      await assert.rejects(f.run(), /changed|head|label|intent|version/i);
      assert.ok(reads > 1, "success must be based on a freshly read PR");
      assert.equal(f.dispatches().length, 0);
      failed(f);
    });
  }
});

test("missing/invalid tree SHAs, truncated snapshots and non-regular headers fail closed", async (t) => {
  for (const kind of [
    "missing tree SHA",
    "invalid tree SHA",
    "head truncated",
    "base truncated",
    "symlink",
    "submodule",
    "missing",
    "duplicate",
  ]) {
    await t.test(kind, async () => {
      const f = fixture();
      const head = f.snapshots.get("head");
      if (kind === "missing tree SHA") delete head.sha;
      if (kind === "invalid tree SHA") head.sha = "not-a-tree-sha";
      if (kind.endsWith("truncated"))
        f.snapshots.get(kind.startsWith("base") ? "base" : "head").truncated =
          true;
      if (kind === "symlink") head.tree[0].mode = "120000";
      if (kind === "submodule")
        Object.assign(head.tree[0], { type: "commit", mode: "160000" });
      if (kind === "missing") head.tree.shift();
      if (kind === "duplicate") head.tree.push(clone(head.tree[0]));
      await assert.rejects(f.run(), /tree|truncated|regular.file|header/i);
      noBranchActivity(f);
      failed(f);
    });
  }
});

test("permission and uncertain mutation errors fail closed without repeating writes", async (t) => {
  for (const [method, path] of [
    ["POST", "/graphql"],
    ["POST", `${root}/actions/workflows/firmware.yml/dispatches`],
    ["PUT", `${root}/pulls/7/update-branch`],
  ]) {
    await t.test(path, async () => {
      const options = {
        before(call) {
          if (call.method === method && call.path === path)
            throw Error("403 or uncertain accepted write");
        },
      };
      const f = method === "PUT" ? behind(options) : fixture(options);
      await assert.rejects(f.run(), /403 or uncertain/);
      assert.equal(
        f.calls.filter((call) => call.method === method && call.path === path)
          .length,
        1,
      );
      failed(f);
    });
  }
});

test("closed/non-main PRs are skipped, and the same-repository default branch is refused", async () => {
  for (const change of [
    { state: "closed" },
    { base: { ref: "release", sha: "base" } },
  ]) {
    const f = fixture();
    Object.assign(f.state.pr, change);
    assert.deepEqual(await f.run(), { number: 7, action: "skipped" });
    assert.equal(f.calls.length, 1);
  }
  const f = fixture();
  f.state.pr.head.ref = "main";
  await assert.rejects(f.run(), /default branch/i);
  noBranchActivity(f);
});

test("reconcileSdkVersions paginates and attempts every PR despite individual failures", async (t) => {
  t.mock.method(console, "log", () => {});
  const visited = [];
  const pages = [];
  const request = async (method, path) => {
    assert.equal(method, "GET");
    const page = path.match(
      /\/pulls\?state=open&base=main&per_page=100&page=(\d+)$/,
    )?.[1];
    if (page) {
      pages.push(Number(page));
      return page === "1"
        ? Array.from({ length: 100 }, (_, i) => ({ number: i + 1 }))
        : [{ number: 101 }];
    }
    const number = Number(path.match(/\/pulls\/(\d+)$/)?.[1]);
    assert.ok(number);
    visited.push(number);
    if ([1, 99].includes(number)) throw Error(`denied ${number}`);
    return { state: "closed", base: { ref: "main" } };
  };
  await assert.rejects(
    reconcileSdkVersions({ repository, request, wait: immediate }),
    (error) => {
      assert.match(error.message, /PR #1: denied 1/);
      assert.match(error.message, /PR #99: denied 99/);
      return true;
    },
  );
  assert.deepEqual(pages, [1, 2]);
  assert.deepEqual(
    visited,
    Array.from({ length: 101 }, (_, i) => i + 1),
  );
});
