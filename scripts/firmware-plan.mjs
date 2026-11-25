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
const sharedInputs = {
  bootloader: [
    "scripts/package-board-bootloader.py",
    ".github/workflows/build-board.yml",
  ],
  micropython: [
    "firmware/pyrt/overlay/**",
    "firmware/pyrt/upstream.json",
    "firmware/ble.defaults",
    "firmware/pyrt/apply_overlay.py",
    "firmware/components/okou_agent/**",
    ".github/actions/build-micropython/**",
  ],
  native: ["firmware/components/okou_agent/**"],
  setup: [
    "scripts/package-setup.py",
    "tools/ble-wifi/provision_usb.py",
    ".github/actions/build-kit/**",
  ],
};
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
    const match =
      /^firmware\/bootloader\/([a-z0-9]+)-(\d+)mb(-usb-cdc)?\/sdkconfig\.defaults$/.exec(
        path,
      );
    if (!match) continue;
    const [, target, flash, transport = ""] = match;
    if (Number(flash) < 8)
      throw Error("EspHub requires at least 8 MiB Flash: " + path);
    const variant = `${target}-${flash}mb${transport}`;
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
      /^firmware\/board\/([^/]+)\/([^/]+)\/ci\.json$/.exec(path);
    if (!match) continue;
    const dir = posix.dirname(path);
    const board = boardId(match[1], match[2]);
    const { chip, flash_mb, bootloader } = json(dir + "/board.json");
    const jobs = json(path),
      tasks = {};
    const sharedBootloader = variants.get(bootloader);
    {
      if (
        sharedBootloader?.target !== chip ||
        sharedBootloader?.flashMb !== flash_mb
      )
        throw Error("Invalid bootloader for " + board);
      const layouts = matching([`${dir}/layout/*.csv`]);
      if (!files.has(`${dir}/layout/partitions.csv`))
        throw Error("Missing partition layout for " + board);
      const partitions = layouts.map(([file, entry]) => {
        if (!entry.startsWith("120000 ")) return entry;
        const target = posix.join(
          posix.dirname(file),
          git("show", ref + ":" + file),
        );
        if (!files.has(target)) throw Error("Unknown layout " + target);
        return files.get(target);
      });
      tasks.bootloader = {
        kind: "bootloader",
        runner: "ubuntu-24.04",
        container: sharedBootloader.container,
        fingerprint: digest([
          board,
          namingInputs,
          chip,
          sharedBootloader.fingerprint,
          layouts,
          partitions,
          ...matching(sharedInputs.bootloader),
        ]),
      };
    }
    const runtime = jobs.native ? "native" : "micropython";
    for (const [kind, job] of Object.entries(jobs)) {
      if (
        !Object.hasOwn(sharedInputs, kind) ||
        kind === "bootloader" ||
        !files.has(`${dir}/${kind}/action.yml`)
      )
        throw Error(`Invalid CI task ${board}/${kind}`);
      const patterns = [
        ...sharedInputs[kind],
        `${dir}/${kind}/**`,
        ...(job.inputs ?? []),
        ...(kind === "setup" && runtime === "micropython"
          ? [`${dir}/main.py`]
          : []),
        ...(kind === "native"
          ? [`${dir}/layout/*.csv`, `firmware/partitions/${flash_mb}mb.csv`]
          : []),
      ];
      const sources = matching(patterns);
      tasks[kind] = {
        kind,
        runner: job.runner ?? "ubuntu-24.04",
        container: job.container ?? "",
        fingerprint: digest([
          board,
          namingInputs,
          chip,
          kind === "micropython" ? bootloader : null,
          job,
          sources,
        ]),
      };
    }
    if (tasks.setup) {
      if (!tasks[runtime])
        throw Error("USB setup requires both component tasks: " + board);
      tasks.setup.fingerprint = digest([
        tasks.setup.fingerprint,
        tasks.bootloader.fingerprint,
        tasks[runtime].fingerprint,
      ]);
    }
    boards[board] = {
      runtime,
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
    { chip, tasks, bootloader, blFingerprint, runtime },
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
      for (const kind of ["bootloader", runtime]) {
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
            runtime: input(runtime),
            runtime_kind: runtime,
          }
        : null,
    });
  }
  return matrix;
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
    version: "1.2." + process.env.GITHUB_RUN_NUMBER,
  };
  if (manual && !options.board) throw Error("Select a board for a manual build");
  const plans = boardPlans(before.boards, after.boards, options);
  const cohorts = new Map();
  for (const plan of plans) {
    const needsBootloader = plan.components.some(({ kind }) => kind === "bootloader");
    const name = needsBootloader ? plan.bootloader : "runtime-only";
    if (!cohorts.has(name)) {
      cohorts.set(name, {
        name,
        bootloader: needsBootloader
          ? { ...after.variants.get(name), version: options.version }
          : null,
        boards: [],
      });
    }
    cohorts.get(name).boards.push(plan);
  }
  appendFileSync(
    process.env.GITHUB_OUTPUT,
    `cohorts=${JSON.stringify([...cohorts.values()])}\n`,
  );
  console.log(
    plans.length
      ? plans
          .map((p) => p.board + ": " + p.jobs.map((j) => j.kind).join(", "))
          .join("\n")
      : "No firmware inputs changed.",
  );
}
