import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, appendFileSync } from "node:fs";
import { matchesGlob, posix, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { boardId, boardFileStem } from "../shared/board-id.js";
import { releaseTag } from "../shared/firmware-releases.js";
import { coverageCommit, isAncestor } from "./firmware-coverage.mjs";

const bootloaderInputs = [
  "firmware/bootloader/common/**",
  "firmware/components/**",
  "firmware/sdkconfig.common",
  "firmware/version.txt",
  "scripts/build-bootloader.sh",
  "scripts/package-bootloader.py",
  ".github/workflows/build-bootloader.yml",
];
const appInputs = [
  "firmware/components/**", "firmware/app/**", "scripts/build-example.py",
];
const kitInputs = [
  "scripts/package-setup.py", "scripts/board-catalog.mjs", "shared/board-examples.js",
  "tools/ble-wifi/provision_usb.py", ".github/actions/build-kit/**",
];
const digest = (value) =>
  createHash("sha256").update(JSON.stringify(value)).digest("hex");

export function profiles(ref, cwd = process.cwd()) {
  return firmwareProfiles(ref, { cwd }).boards;
}

function firmwareProfiles(ref, { cwd = process.cwd(), pullRequest = false } = {}) {
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
    // Variants may carry a hardware suffix such as -uart or -opi.
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
    const { examples = [], shared_example_inputs = {}, ...settings } = json(path);
    const { chip, flash_mb, bootloader, default_example } = settings;
    const sharedBootloader = variants.get(bootloader);
    if (sharedBootloader?.target !== chip || sharedBootloader.flashMb !== flash_mb)
      throw Error("Invalid bootloader for " + board);
    // On PRs, track optional examples separately from the default app shipped in the kit.
    const defaultExample = pullRequest && examples.find(example => example.id === default_example);
    const partition = `firmware/partitions/${flash_mb}mb.csv`;
    const boardInputs = defaultExample
      ? [settings, defaultExample, matching([partition])]
      : matching([path, partition]);
    const optional = defaultExample ? examples.filter(example => example.id !== default_example) : [];
    const optionalSources = new Map(optional.map(({ id }) => [id, []]));
    const root = `${dir}/examples/`;
    const exampleIds = new Set(examples.map(({ id }) => id));
    const sharedOwners = new Map();
    for (const [relative, owners] of Object.entries(shared_example_inputs)) {
      const file = root + relative;
      if (!files.has(file) || exampleIds.has(relative.split("/", 1)[0]) ||
          !Array.isArray(owners) || !owners.length || owners.some(id => !exampleIds.has(id)))
        throw Error(`Invalid shared example input for ${board}: ${relative}`);
      if (defaultExample) sharedOwners.set(file, owners);
    }
    const appSources = [];
    for (const source of matching([...appInputs, root + "**"])) {
      const [file] = source;
      const id = file.startsWith(root) ? file.slice(root.length).split("/", 1)[0] : null;
      // Unscoped shared files and inputs to the default app still affect the whole board.
      for (const owner of sharedOwners.get(file) ?? [id]) {
        const inputs = optionalSources.get(owner) ?? appSources;
        inputs.push(source);
      }
    }
    const appContainer = chip === "esp32s31"
      ? "espressif/idf:release-v6.2@sha256:0e714292d8b4e6e75cb42e25461716221448858e125d96dbfea09732964c412d"
      : "espressif/idf:v5.5.4";
    const tasks = {
      bootloader: {
        kind: "bootloader", container: sharedBootloader.container,
        fingerprint: digest([board, namingInputs, boardInputs, sharedBootloader.fingerprint,
          matching(["scripts/package-board-bootloader.py"])]),
      },
      app: {
        kind: "app", container: appContainer,
        fingerprint: digest([board, namingInputs, boardInputs, appContainer, appSources]),
      },
    };
    // The kit fingerprint covers both components, so any board change rebuilds all three.
    tasks.setup = {
      kind: "setup",
      fingerprint: digest([tasks.bootloader.fingerprint, tasks.app.fingerprint, matching(kitInputs)]),
    };
    boards[board] = { chip, tasks, bootloader };
    if (optional.length) {
      boards[board].exampleFingerprints = Object.fromEntries(optional.map(example => [
        example.id, digest([example, optionalSources.get(example.id)]),
      ]));
    }
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
    { chip, tasks, bootloader, exampleFingerprints },
  ] of Object.entries(after)) {
    if (selected && selected !== board) continue;
    const changed = manual || Object.entries(tasks).some(
      ([kind, task]) => before[board]?.tasks[kind]?.fingerprint !== task.fingerprint,
    );
    // An unchanged board on a PR still compiles its changed optional examples.
    let selectedExamples;
    if (!changed) {
      selectedExamples = Object.keys(exampleFingerprints ?? {}).filter(
        id => exampleFingerprints[id] !== before[board]?.exampleFingerprints?.[id],
      );
      if (!selectedExamples.length) continue;
    }
    const jobs = (changed ? Object.values(tasks) : [tasks.app])
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
      ...(selectedExamples && { examples: selectedExamples }),
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

export function assignApplicationShards(plans) {
  const shards = new Map();
  for (const plan of plans) {
    const app = plan.components.find(({ kind }) => kind === "app");
    if (!app) continue;
    const hash = createHash("sha256").update(plan.board).digest("hex");
    const shard = Number(BigInt("0x" + hash) % 20n);
    if (!shards.has(shard)) shards.set(shard, { shard, groups: {} });
    const { groups } = shards.get(shard);
    (groups[app.container] ??= []).push(plan.board);
    plan.appShard = shard;
  }
  return [...shards.values()].sort((a, b) => a.shard - b.shard);
}

// GitHub schedules one running job per repository-wide concurrency group.
// Start at a different group on each run to spread small or manual builds.
export function buildCohorts(plans, variants, version, runNumber) {
  let index = runNumber;
  const nextGroup = () => `firmware-build-slot-${index++ % 100}`;
  const cohorts = new Map();
  for (const plan of plans) {
    const needsBootloader = plan.components.some(({ kind }) => kind === "bootloader");
    const name = needsBootloader ? plan.bootloader : "examples";
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

// Keep only boards whose current inputs already have coverage throughout
// the interval. Looking at intermediate main snapshots also catches a change
// that was built (or partly published) and then reverted during a backlog.
export function unchangedMainBoards(covered, head, current, cwd = process.cwd()) {
  if (!covered) return {};
  if (covered === head) return current;
  if (!isAncestor(covered, head, cwd)) {
    // An older queued run may start after a newer one recorded coverage.
    // A real main rewind, however, needs a build at the new head.
    if (isAncestor(head, covered, cwd) && isAncestor(covered, "refs/remotes/origin/main", cwd)) return null;
    return {};
  }
  const revisions = execFileSync("git", ["rev-list", "--first-parent", covered + ".." + head],
    { cwd, encoding: "utf8" }).trim().split("\n");
  const unchanged = { ...current };
  for (const ref of [covered, ...revisions.slice(1)]) {
    let previous;
    try {
      previous = profiles(ref, cwd);
    } catch (error) {
      // An invalid older manifest must not prevent the corrected head from building.
      console.warn("Cannot compare historical board inputs at " + ref + ": " + error.message);
      return {};
    }
    for (const [board, profile] of Object.entries(unchanged)) {
      if (previous[board]?.tasks.setup.fingerprint !== profile.tasks.setup.fingerprint) delete unchanged[board];
    }
    if (!Object.keys(unchanged).length) break;
  }
  return unchanged;
}

export function isMainBuildBatch(event, env) {
  if (env.GITHUB_REPOSITORY !== "esphub-ai/esphub-firmware" || env.GITHUB_REF !== "refs/heads/main") return false;
  return env.GITHUB_EVENT_NAME === "push" ||
    (env.GITHUB_EVENT_NAME === "workflow_dispatch" && !event.inputs?.board && !event.inputs?.pr_number);
}

export function firmwareEventContext(event, env) {
  const dispatchedPr =
    env.GITHUB_EVENT_NAME === "workflow_dispatch" && !!event.inputs?.pr_number;
  if (dispatchedPr && !/^[a-f0-9]{40}$/.test(env.PR_CI_BASE_SHA ?? ""))
    throw Error("PR CI dispatch requires a validated base SHA");
  return {
    pullRequest: env.GITHUB_EVENT_NAME === "pull_request" || dispatchedPr,
    manual: env.GITHUB_EVENT_NAME === "workflow_dispatch" && !dispatchedPr && !isMainBuildBatch(event, env),
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
  const { manual, pullRequest, beforeRef, afterRef } = firmwareEventContext(
    event,
    process.env,
  );
  const after = firmwareProfiles(afterRef, { pullRequest });
  const mainBatch = isMainBuildBatch(event, process.env);
  const covered = mainBatch ? coverageCommit() : "";
  let before;
  if (mainBatch) before = unchangedMainBoards(covered, afterRef, after.boards);
  else if (process.env.GITHUB_EVENT_NAME === "push" && /^0+$/.test(beforeRef ?? "")) before = after.boards;
  else before = firmwareProfiles(beforeRef, { pullRequest }).boards;
  const options = {
    manual,
    board: event.inputs?.board ?? "",
    version: "2.0." + process.env.GITHUB_RUN_NUMBER,
  };
  if (manual && !options.board) throw Error("Select a board for a manual build");
  const plans = before === null ? [] : boardPlans(before, after.boards, options);
  const appShards = mainBatch ? assignApplicationShards(plans) : [];
  appendFileSync(process.env.GITHUB_OUTPUT, `app_shards=${JSON.stringify(appShards)}\n`);
  const cohorts = buildCohorts(plans, after.variants, options.version, Number(process.env.GITHUB_RUN_NUMBER));
  appendFileSync(process.env.GITHUB_OUTPUT, `cohorts=${JSON.stringify(cohorts)}\n`);
  if (mainBatch && before !== null) {
    const expected = Object.fromEntries(cohorts.map(({ name, boards }) => [
      name, boards.map(({ board, jobs }) => [board, jobs.map(task => task.kind)]),
    ]));
    appendFileSync(process.env.GITHUB_OUTPUT, `coverage_base=${covered}\nexpected_builds=${JSON.stringify(expected)}\n`);
  }
  console.log(
    plans.length
      ? plans
          .map(p => p.board + ": " + (p.examples ?? p.jobs.map(j => j.kind)).join(", "))
          .join("\n")
      : "No firmware inputs changed.",
  );
}
