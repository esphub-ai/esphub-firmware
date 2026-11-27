// Verify that a publication comes from a trusted main firmware build and that the
// selected board app or bootloader set built successfully in it.
import { execFileSync } from "node:child_process";
import { appendFileSync } from "node:fs";
import { boardFileStem } from "../shared/board-id.js";
import { bootloaders, profiles } from "./firmware-plan.mjs";

const repo = "esphub-ai/esphub-firmware";
const id = process.env.SOURCE_RUN ?? "";
const board = process.env.BOARD;
const bootloader = process.env.BOOTLOADER;
if (!/^\d+$/.test(id)) throw Error("Invalid source run ID");
if (!board === !bootloader) throw Error("Select exactly one board or bootloader");
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
const artifacts = (prefix) => new Set(
  api(
    "/artifacts?per_page=100",
    `[.artifacts[] | select(.expired == false and (.name | startswith(${JSON.stringify(prefix)}))) | .name]`,
  ).flat(),
);
const jobs = (segment) => api(
  "/jobs?per_page=100",
  `[.jobs[] | select(.name | contains(${JSON.stringify(segment)})) | {name, conclusion}]`,
).flat();
const succeeded = (matches) =>
  matches.some((job) => job.conclusion === "success") &&
  !matches.some((job) => job.conclusion !== "success");

const outputs = ["sha=" + run.head_sha];
if (bootloader) {
  if (!Object.hasOwn(bootloaders(run.head_sha), bootloader))
    throw Error("Bootloader does not exist in the build revision");
  const artifact = "bl-" + bootloader;
  const name = `${bootloader} / bootloader / build`;
  if (!artifacts(artifact).has(artifact) || !succeeded(jobs(name).filter((job) => job.name === name)))
    throw Error("Incomplete build or artifact for bootloader " + bootloader);
  outputs.push("artifact=" + artifact);
} else {
  const boards = profiles(run.head_sha);
  if (!Object.hasOwn(boards, board))
    throw Error("Board does not exist in the build revision");
  const profile = boards[board];
  const stem = boardFileStem(board);
  const name = `/ ${board} / app`;
  if (!artifacts(stem + ".").has(stem + ".app") ||
      !succeeded(jobs(`/ ${board} / `).filter((job) => job.name.endsWith(name))))
    throw Error("Incomplete build or artifact for app");
  outputs.push(
    "stem=" + stem,
    "profile=" + JSON.stringify({
      board,
      chip: profile.chip,
      jobs: [{ kind: "app", fingerprint: profile.tasks.app.fingerprint, version: "2.0." + run.run_number }],
    }),
  );
}
appendFileSync(process.env.GITHUB_OUTPUT, outputs.join("\n") + "\n");
