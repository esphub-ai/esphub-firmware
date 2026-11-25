import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { validatePrCiContext } from "../scripts/pr-ci-context.mjs";

const repository = "okou-ai/esphub";
const repositoryId = 100;
const baseSha = "a".repeat(40);
const headSha = "b".repeat(40);
const headRef = "feature/sdk-version";
const options = {
  repository,
  repositoryId: String(repositoryId),
  prNumber: "7",
  ref: `refs/heads/${headRef}`,
  sha: headSha,
};
const pullRequest = () => ({
  state: "open",
  base: {
    ref: "main",
    sha: baseSha,
    repo: { id: repositoryId, full_name: repository },
  },
  head: {
    ref: headRef,
    sha: headSha,
    repo: { id: repositoryId, full_name: repository },
  },
});

function fixture(pr = pullRequest()) {
  const calls = [];
  return {
    calls,
    request: async (...args) => {
      calls.push(args);
      assert.deepEqual(args, ["GET", `/repos/${repository}/pulls/7`]);
      return structuredClone(pr);
    },
  };
}

test("validates a live same-repository branch and SHA with one read and no writes", async () => {
  const { request, calls } = fixture();
  assert.deepEqual(await validatePrCiContext({ ...options, request }), {
    baseSha,
    headSha,
  });
  assert.deepEqual(calls, [["GET", `/repos/${repository}/pulls/7`]]);
});

test("accepts numeric IDs/PR numbers and case-insensitive repository names", async () => {
  const pr = pullRequest();
  pr.base.repo.full_name = pr.head.repo.full_name = "Okou-AI/EspHub";
  const { request } = fixture(pr);
  assert.deepEqual(
    await validatePrCiContext({
      ...options,
      repositoryId,
      prNumber: 7,
      request,
    }),
    { baseSha, headSha },
  );
});

test("empty or omitted PR input returns empty context without requiring other values or network", async () => {
  for (const prNumber of ["", undefined]) {
    let calls = 0;
    const result = await validatePrCiContext({
      prNumber,
      request: async () => {
        calls++;
        throw Error("Network must not be called");
      },
    });
    assert.deepEqual(result, { baseSha: "", headSha: "" });
    assert.equal(calls, 0);
  }
});

for (const [name, changes] of [
  ["PR whitespace", { prNumber: " " }],
  ["PR trailing newline", { prNumber: "7\n" }],
  ["PR leading zero", { prNumber: "07" }],
  ["PR exponent", { prNumber: "1e2" }],
  ["PR fraction", { prNumber: 7.5 }],
  ["PR zero", { prNumber: 0 }],
  ["PR negative", { prNumber: -1 }],
  ["PR unsafe integer", { prNumber: "9007199254740992" }],
  ["PR null", { prNumber: null }],
  ["PR object", { prNumber: { toString: () => "7" } }],
  ["repository undefined", { repository: undefined }],
  ["repository object", { repository: { toString: () => repository } }],
  ["repository trailing newline", { repository: repository + "\n" }],
  ["repository trailing CRLF", { repository: repository + "\r\n" }],
  ["repository path traversal", { repository: "okou-ai/.." }],
  ["repository extra path", { repository: repository + "/pulls" }],
  ["repository ID unsafe", { repositoryId: "9007199254740992" }],
  ["repository ID malformed", { repositoryId: "100\n" }],
  ["short SHA", { sha: "b".repeat(39) }],
  ["nonhex SHA", { sha: "g".repeat(40) }],
  ["SHA trailing newline", { sha: headSha + "\n" }],
  ["SHA non-string", { sha: { toString: () => headSha } }],
]) {
  test(`rejects malformed ${name} before any request, without fallback`, async () => {
    const { request, calls } = fixture();
    await assert.rejects(
      validatePrCiContext({ ...options, ...changes, request }),
      /Invalid/,
    );
    assert.deepEqual(calls, []);
  });
}

for (const [name, change, message] of [
  [
    "wrong head SHA",
    (pr) => {
      pr.head.sha = "c".repeat(40);
    },
    /current PR head/,
  ],
  [
    "closed PR",
    (pr) => {
      pr.state = "closed";
    },
    /must be open/,
  ],
  [
    "wrong base branch",
    (pr) => {
      pr.base.ref = "release";
    },
    /target main/,
  ],
  [
    "base repository ID mismatch",
    (pr) => {
      pr.base.repo.id = 200;
    },
    /expected repository/,
  ],
  [
    "base repository name mismatch",
    (pr) => {
      pr.base.repo.full_name = "other/esphub";
    },
    /expected repository/,
  ],
  [
    "fork",
    (pr) => {
      pr.head.repo = { id: 200, full_name: "contributor/esphub" };
    },
    /forks/,
  ],
  [
    "head ID mismatch despite matching name",
    (pr) => {
      pr.head.repo.id = 200;
    },
    /forks/,
  ],
  [
    "head name mismatch despite matching ID",
    (pr) => {
      pr.head.repo.full_name = "other/esphub";
    },
    /forks/,
  ],
  [
    "deleted head repository",
    (pr) => {
      pr.head.repo = null;
    },
    /forks/,
  ],
  [
    "main as head",
    (pr) => {
      pr.head.ref = "main";
    },
    /head branch/,
  ],
  [
    "empty head ref",
    (pr) => {
      pr.head.ref = "";
    },
    /head branch/,
  ],
  [
    "malformed base SHA",
    (pr) => {
      pr.base.sha = "not-a-sha";
    },
    /base SHA/,
  ],
  [
    "base SHA trailing newline",
    (pr) => {
      pr.base.sha += "\n";
    },
    /base SHA/,
  ],
]) {
  test(`rejects ${name} using only a GET`, async () => {
    const pr = pullRequest();
    change(pr);
    const { request, calls } = fixture(pr);
    await assert.rejects(validatePrCiContext({ ...options, request }), message);
    assert.equal(calls.length, 1);
    assert.equal(calls[0][0], "GET");
  });
}

test("rejects other branches, tags, and PR merge refs", async () => {
  for (const ref of [
    "refs/heads/main",
    "refs/heads/other",
    `refs/tags/${headRef}`,
    "refs/pull/7/merge",
  ]) {
    const { request } = fixture();
    await assert.rejects(
      validatePrCiContext({ ...options, ref, request }),
      /head branch/,
    );
  }
});

test("propagates lookup failure instead of returning empty context", async () => {
  await assert.rejects(
    validatePrCiContext({
      ...options,
      request: async () => {
        throw Error("API unavailable");
      },
    }),
    /API unavailable/,
  );
});

const script = fileURLToPath(
  new URL("../scripts/pr-ci-context.mjs", import.meta.url),
);
function runCli(t, { pr = pullRequest(), env = {}, existing = "" } = {}) {
  const directory = mkdtempSync(join(tmpdir(), "esphub-pr-ci-context-"));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const output = join(directory, "output");
  writeFileSync(output, existing);
  // Intercept fetch in the child process; these CLI tests never access a network.
  const preload = `globalThis.fetch = async (url, init) => {
    if (url !== "https://api.github.com/repos/${repository}/pulls/7" || init.method !== "GET") throw Error("Unexpected API request");
    return { ok: true, status: 200, json: async () => (${JSON.stringify(pr)}) };
  };`;
  const result = spawnSync(
    process.execPath,
    ["--import", "data:text/javascript," + encodeURIComponent(preload), script],
    {
      encoding: "utf8",
      env: {
        ...process.env,
        GH_TOKEN: "fixture-token-not-a-real-credential",
        GITHUB_REPOSITORY: repository,
        GITHUB_REPOSITORY_ID: String(repositoryId),
        PR_NUMBER: "7",
        GITHUB_REF: options.ref,
        GITHUB_SHA: headSha,
        GITHUB_OUTPUT: output,
        ...env,
      },
    },
  );
  return { ...result, output: readFileSync(output, "utf8") };
}

test("CLI appends validated output without replacing existing output", (t) => {
  const result = runCli(t, { existing: "existing=value\n" });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(
    result.output,
    `existing=value\nbase_sha=${baseSha}\nhead_sha=${headSha}\n`,
  );
});

test("CLI validation failure leaves output untouched", (t) => {
  const result = runCli(t, {
    env: { GITHUB_SHA: "c".repeat(40) },
    existing: "existing=value\n",
  });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /current PR head/);
  assert.equal(result.output, "existing=value\n");
});

test("CLI empty input needs no token or network and emits empty context", (t) => {
  const result = runCli(t, {
    env: { PR_NUMBER: "", GH_TOKEN: "", GITHUB_REPOSITORY: "", GITHUB_SHA: "" },
  });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.output, "base_sha=\nhead_sha=\n");
});
