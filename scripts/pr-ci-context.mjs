import { appendFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

function positiveInteger(value, name) {
  if (
    !["string", "number"].includes(typeof value) ||
    /^[1-9]\d*$/.exec(String(value))?.[0] !== String(value) ||
    !Number.isSafeInteger(Number(value))
  )
    throw Error(`Invalid ${name}: expected a positive safe integer`);
  return Number(value);
}

const isSha = (value) =>
  typeof value === "string" && /^[a-fA-F0-9]{40}$/.exec(value)?.[0] === value;

export async function validatePrCiContext({
  repository,
  repositoryId,
  prNumber = "",
  ref,
  sha,
  request,
}) {
  if (prNumber === "") return { baseSha: "", headSha: "" };

  const number = positiveInteger(prNumber, "PR number");
  const id = positiveInteger(repositoryId, "repository ID");
  if (
    typeof repository !== "string" ||
    /^[a-zA-Z0-9-]+\/[a-zA-Z0-9_.-]+$/.exec(repository)?.[0] !== repository ||
    [".", ".."].includes(repository.split("/")[1])
  )
    throw Error("Invalid GitHub repository");
  if (!isSha(sha))
    throw Error("Invalid run SHA: expected 40 hexadecimal characters");

  const pr = await request("GET", `/repos/${repository}/pulls/${number}`);
  const sameRepository = (repo) =>
    repo?.id === id &&
    typeof repo.full_name === "string" &&
    repo.full_name.toLowerCase() === repository.toLowerCase();
  if (pr?.state !== "open") throw Error("PR must be open");
  if (pr.base?.ref !== "main" || !sameRepository(pr.base?.repo))
    throw Error("PR must target main in the expected repository");
  if (!sameRepository(pr.head?.repo))
    throw Error(
      "PR head must belong to the expected repository; forks are not dispatched",
    );
  if (
    typeof pr.head.ref !== "string" ||
    !pr.head.ref ||
    pr.head.ref === "main" ||
    ref !== `refs/heads/${pr.head.ref}`
  )
    throw Error("Run ref must match the PR head branch and must not be main");
  if (sha !== pr.head.sha)
    throw Error("Run SHA must match the current PR head");
  if (!isSha(pr.base.sha)) throw Error("Invalid PR base SHA");
  return { baseSha: pr.base.sha, headSha: pr.head.sha };
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const context = await validatePrCiContext({
    repository: process.env.GITHUB_REPOSITORY,
    repositoryId: process.env.GITHUB_REPOSITORY_ID,
    prNumber: process.env.PR_NUMBER ?? "",
    ref: process.env.GITHUB_REF,
    sha: process.env.GITHUB_SHA,
    request: async (method, path) => {
      if (!process.env.GH_TOKEN) throw Error("Missing GH_TOKEN");
      const response = await fetch(`https://api.github.com${path}`, {
        method,
        headers: {
          Accept: "application/vnd.github+json",
          Authorization: `Bearer ${process.env.GH_TOKEN}`,
          "X-GitHub-Api-Version": "2022-11-28",
        },
      });
      if (!response.ok)
        throw Error(`GitHub PR lookup failed: HTTP ${response.status}`);
      return response.json();
    },
  });
  if (!process.env.GITHUB_OUTPUT) throw Error("Missing GITHUB_OUTPUT");
  await appendFile(
    process.env.GITHUB_OUTPUT,
    `base_sha=${context.baseSha}\nhead_sha=${context.headSha}\n`,
  );
}
