import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, appendFileSync } from "node:fs";
import { matchesGlob, posix, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { boardId, boardFileStem } from "../shared/board-id.js";
import { releaseTag } from "../shared/firmware-releases.js";

const bootloaderInputs = [
  "firmware/bootloader/common/**",
  "firmware/components/**",
  "firmware/sdkconfig.common",
  "firmware/ble.defaults",
  "firmware/version.txt",
  "scripts/build-bootloader.sh",
  "scripts/package-bootloader.py",
  ".github/workflows/build-bootloader.yml",
];
const appInputs = [
  "firmware/components/**", "firmware/app/**", "scripts/build-example.py",
  ".github/workflows/build-board.yml", "scripts/firmware-plan.mjs",
];
const kitInputs = [
  "scripts/package-setup.py", "scripts/board-catalog.mjs", "shared/board-examples.js",
  "tools/ble-wifi/provision_usb.py", ".github/actions/build-kit/**",
];
const digest = (value) =>
  createHash("sha256").update(JSON.stringify(value)).digest("hex");

export function profiles(ref, cwd = process.cwd()) {
  return firmwareProfiles(ref, cwd).boards;
}

function firmwareProfiles(ref, cwd = process.cwd()) {
  if (!ref || /^0+$/.test(ref)) return { boards: {}, variants: new Map() };
  const git = (...args) =>
    execFileSync("git", args, {
      cwd,
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
    });
  const files = new Map(
    git("ls-tree", "-r", "-z", ref)
      .split("\0")
      .filter(Boolean)
      .map((row) => {
        const sep = row.indexOf("\t");
        return [row.slice(sep + 1), row.slice(0, sep)];
      }),
  );
  const json = (file) => JSON.parse(git("show", ref + ":" + file));
  const boards = {};
  const matching = (patterns) =>
    [...files].filter(
      ([file]) =>
        !file.endsWith(".md") &&
        patterns.some((pattern) => matchesGlob(file, pattern)),
    );
  const variants = new Map();
  for (const path of files.keys()) {
    // The base snapshot may still contain an older transport-suffixed variant.
    const match =
      /^firmware\/bootloader\/(([a-z0-9]+)-(\d+)mb(?:-[a-z0-9-]+)?)\/sdkconfig\.defaults$/.exec(
        path,
      );
    if (!match) continue;
    const [, variant, target, flash] = match;
    if (Number(flash) < 8)
      throw Error("EspHub requires at least 8 MiB Flash: " + path);
    const container =
      target === "esp32s31"
        ? "espressif/idf:v6.1.1"
        : target === "esp32c5" || target === "esp32c61"
          ? "espressif/idf:v5.5.4"
          : "espressif/idf:v5.4.1";
    const sources = matching([
      ...bootloaderInputs,
      `firmware/bootloader/${variant}/**`,
      `firmware/partitions/${flash}mb.csv`,
    ]);
    variants.set(variant, {
      variant,
      container,
      target,
      flashMb: Number(flash),
      fingerprint: digest([variant, container, sources]),
    });
  }
  const namingInputs = matching(["shared/board-id.js", "shared/firmware-releases.js"]);
  for (const path of files.keys()) {
    const match =
      /^firmware\/board\/([^/]+)\/([^/]+)\/board\.json$/.exec(path);
    if (!match) continue;
    const dir = posix.dirname(path);
    const board = boardId(match[1], match[2]);
    const { chip, flash_mb, bootloader } = json(dir + "/board.json");
    const sharedBootloader = variants.get(bootloader);
    if (sharedBootloader?.target !== chip || sharedBootloader.flashMb !== flash_mb)
      throw Error("Invalid bootloader for " + board);
    const boardInputs = matching([path, `firmware/partitions/${flash_mb}mb.csv`]);
    const runner = "ubuntu-24.04";
    const appContainer = chip === "esp32s31"
      ? "espressif/idf:release-v6.2@sha256:0e714292d8b4e6e75cb42e25461716221448858e125d96dbfea09732964c412d"
      : "espressif/idf:v5.5.4";
    const tasks = {
      bootloader: {
        kind: "bootloader", runner, container: sharedBootloader.container,
        fingerprint: digest([board, namingInputs, boardInputs, sharedBootloader.fingerprint,
          matching(["scripts/package-board-bootloader.py", ".github/workflows/build-board.yml"])]),
      },
      app: {
        kind: "app", runner, container: appContainer,
        fingerprint: digest([board, namingInputs, boardInputs, appContainer,
          matching([...appInputs, `${dir}/examples/**`])]),
      },
    };
    tasks.setup = {
      kind: "setup", runner, container: "",
      fingerprint: digest([tasks.bootloader.fingerprint, tasks.app.fingerprint, matching(kitInputs)]),
    };
    boards[board] = {
      chip,
      tasks,
      bootloader,
      blFingerprint: sharedBootloader?.fingerprint,
    };
  }
  return { boards, variants };
}

export function boardPlans(
  before,
  after,
  { version, manual = false, board: selected = "" },
) {
  if (selected && !Object.hasOwn(after, selected))
    throw Error("Unknown board: " + selected);
  const matrix = [];
  for (const [
    board,
    { chip, tasks, bootloader, blFingerprint },
  ] of Object.entries(after)) {
    if (selected && selected !== board) continue;
    const changed = {};
    for (const [kind, task] of Object.entries(tasks)) {
      const touched =
        manual || before[board]?.tasks[kind]?.fingerprint !== task.fingerprint;
      changed[kind] = touched;
    }
    if (!Object.values(changed).some(Boolean)) continue;
    if (changed.setup) {
      for (const kind of ["bootloader", "app"]) {
        changed[kind] = true;
      }
    }
    const jobs = Object.values(tasks)
      .filter((task) => changed[task.kind])
      .map((task) => ({
        ...task,
        tag: releaseTag(task.kind, board, version),
        version,
      }));
    const components = jobs.filter((t) => t.kind !== "setup"),
      kit = jobs.find((t) => t.kind === "setup");
    const input = (kind) =>
      components.find((t) => t.kind === kind);
    matrix.push({
      board,
      fileStem: boardFileStem(board),
      chip,
      jobs,
      bootloader,
      blFingerprint,
      components,
      kit: kit
        ? {
            ...kit,
            bootloader: input("bootloader"),
            app: input("app"),
          }
        : null,
    });
  }
  return matrix;
}

// GitHub schedules one running job per repository-wide concurrency group.
// Start at a different group on each run to spread small or manual builds.
export function buildCohorts(plans, variants, version, runNumber) {
  let index = runNumber;
  const nextGroup = () => `firmware-build-slot-${index++ % 100}`;
  const cohorts = new Map();
  for (const plan of plans) {
    const needsBootloader = plan.components.some(({ kind }) => kind === "bootloader");
    const name = needsBootloader ? plan.bootloader : "app-only";
    if (!cohorts.has(name)) {
      cohorts.set(name, {
        name,
        bootloader: needsBootloader
          ? { ...variants.get(name), version, buildGroup: nextGroup() }
          : null,
        boards: [],
      });
    }
    const components = plan.components.map(task => ({ ...task, buildGroup: nextGroup() }));
    // The kit and publication dispatch run after the components, so they can
    // reuse a component's group without reducing component parallelism.
    cohorts.get(name).boards.push({ ...plan, components });
  }
  return [...cohorts.values()];
}

export function firmwareEventContext(event, env) {
  const dispatchedPr =
    env.GITHUB_EVENT_NAME === "workflow_dispatch" && !!event.inputs?.pr_number;
  if (dispatchedPr && !/^[a-f0-9]{40}$/.test(env.PR_CI_BASE_SHA ?? ""))
    throw Error("PR CI dispatch requires a validated base SHA");
  return {
    manual: env.GITHUB_EVENT_NAME === "workflow_dispatch" && !dispatchedPr,
    beforeRef:
      event.pull_request?.base.sha ||
      (dispatchedPr ? env.PR_CI_BASE_SHA : event.before),
    afterRef: env.FW_HEAD_SHA || env.GITHUB_SHA,
  };
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH));
  const { manual, beforeRef, afterRef } = firmwareEventContext(
    event,
    process.env,
  );
  const after = firmwareProfiles(afterRef);
  const before =
    process.env.GITHUB_EVENT_NAME === "push" && /^0+$/.test(beforeRef ?? "")
      ? after
      : firmwareProfiles(beforeRef);
  const options = {
    manual,
    board: event.inputs?.board ?? "",
    version: "2.0." + process.env.GITHUB_RUN_NUMBER,
  };
  if (manual && !options.board) throw Error("Select a board for a manual build");
  const plans = boardPlans(before.boards, after.boards, options);
  const cohorts = buildCohorts(plans, after.variants, options.version, Number(process.env.GITHUB_RUN_NUMBER));
  appendFileSync(
    process.env.GITHUB_OUTPUT,
    `cohorts=${JSON.stringify(cohorts)}\n`,
  );
  console.log(
    plans.length
      ? plans
          .map((p) => p.board + ": " + p.jobs.map((j) => j.kind).join(", "))
          .join("\n")
      : "No firmware inputs changed.",
  );
}
