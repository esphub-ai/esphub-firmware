import { execFileSync } from "node:child_process";
import { appendFileSync } from "node:fs";
import { boardFileStem } from "../shared/board-id.js";
import { profiles } from "./firmware-plan.mjs";

const repo = "esphub-ai/esphub-firmware";
const id = process.env.SOURCE_RUN ?? "";
const board = process.env.BOARD;
if (!/^\d+$/.test(id)) throw Error("Invalid source run ID");
const api = (path, pageFilter) =>
  execFileSync(
    "gh",
    [
      "api",
      `repos/${repo}/actions/runs/${id}${path}`,
      ...(path ? ["--paginate"] : []),
      "--jq",
      pageFilter,
    ],
    { encoding: "utf8" },
  )
    .trim()
    .split("\n")
    .map((page) => JSON.parse(page));
const [run] = api("", ".");
if (
  run.head_repository.full_name !== repo ||
  run.head_branch !== "main" ||
  !["workflow_dispatch", "push"].includes(run.event) ||
  run.path !== ".github/workflows/firmware.yml" ||
  !/^[a-f0-9]{40}$/.test(run.head_sha) ||
  !Number.isSafeInteger(run.run_number)
) {
  throw Error("Release source is not a trusted main firmware build");
}
execFileSync("git", [
  "merge-base",
  "--is-ancestor",
  run.head_sha,
  process.env.GITHUB_SHA,
]);
const boards = profiles(run.head_sha);
if (!Object.hasOwn(boards, board))
  throw Error("Board does not exist in the build revision");
const profile = boards[board];
const stem = boardFileStem(board);
const names = new Set(
  api(
    "/artifacts?per_page=100",
    `[.artifacts[] | select(.expired == false and (.name | startswith(${JSON.stringify(stem + ".")}))) | .name]`,
  ).flat(),
);
const boardSegment = JSON.stringify(`/ ${board} / `);
const jobs = api(
  "/jobs?per_page=100",
  `[.jobs[] | select(.name | contains(${boardSegment})) | {name, conclusion}]`,
).flat();
const selected = Object.values(profile.tasks).filter(({ kind }) => {
  const name = kind === "setup" ? "USB kit" : kind;
  const matches = jobs.filter((job) =>
    job.name.endsWith(`/ ${board} / ${name}`),
  );
  const hasArtifact = names.has(stem + "." + kind);
  const succeeded = matches.some((job) => job.conclusion === "success");
  if (
    matches.some(
      (job) => job.conclusion !== "success" && job.conclusion !== "skipped",
    ) ||
    hasArtifact !== succeeded
  ) {
    throw Error("Incomplete build or artifact for " + kind);
  }
  return succeeded;
});
if (!selected.length) throw Error("No completed assets for board " + board);
appendFileSync(
  process.env.GITHUB_OUTPUT,
  [
    "stem=" + stem,
    "sha=" + run.head_sha,
    "profile=" +
      JSON.stringify({
        board,
        chip: profile.chip,
        jobs: selected.map(({ kind, fingerprint }) => ({
          kind,
          fingerprint,
          version: "2.0." + run.run_number,
        })),
      }),
    "",
  ].join("\n"),
);
