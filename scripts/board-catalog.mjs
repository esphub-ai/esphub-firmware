import { execFileSync } from "node:child_process";
import { readdir, readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describeExamples } from "../shared/board-examples.js";
import { boardId } from "../shared/board-id.js";

const repository = new URL("../", import.meta.url);

export async function readBoardCatalog() {
  const root = new URL("firmware/board/", repository);
  const boards = {};
  for (const vendor of await readdir(root, { withFileTypes: true })) {
    if (!vendor.isDirectory()) continue;
    const vendorRoot = new URL(vendor.name + "/", root);
    for (const project of await readdir(vendorRoot, { withFileTypes: true })) {
      if (!project.isDirectory()) continue;
      const id = boardId(vendor.name, project.name);
      const directory = new URL(project.name + "/", vendorRoot);
      const file = (name) => readFile(new URL(name, directory), "utf8");
      const {
        name,
        model,
        chip,
        flash_mb,
        bootloader,
        preview = false, default_example, examples,
      } = JSON.parse(await file("board.json"));
      if (
        ![name, model, chip, bootloader].every(
          (v) => typeof v === "string" && v.length,
        )
      )
        throw Error("Invalid metadata for " + id);
      if (!Number.isInteger(flash_mb) || flash_mb < 8)
        throw Error("EspHub requires at least 8 MiB Flash: " + id);
      if (!Array.isArray(examples) || !examples.some(e => e.id === default_example) ||
          new Set(examples.map(e => e.id)).size !== examples.length)
        throw Error("Invalid default or duplicate example for " + id);
      for (const e of examples) {
        if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(e.id) || !e.description?.trim() ||
            !e.entrypoint?.startsWith("examples/" + e.id + "/") || e.entrypoint.includes("..") ||
            !e.upstream?.startsWith("https://"))
          throw Error("Invalid example for " + id);
        await file(e.entrypoint);
      }
      boards[id] = {
        name,
        model,
        chip,
        flash_mb,
        bootloader,
        ...(preview ? { preview: true } : {}),
        default_example, examples,
      };
    }
  }
  if (!Object.keys(boards).length) throw Error("Empty board catalog");
  const header = await readFile(
    new URL("firmware/components/eh_agent/include/eh_agent.h", repository),
    "utf8",
  );
  const sdk_version = header.match(
    /^#define EH_SDK_VERSION "([^"]+)"$/m,
  )?.[1];
  if (!sdk_version) throw Error("Missing EspHub SDK version");
  const revision = execFileSync("git", ["rev-parse", "HEAD"], {
    cwd: repository,
    encoding: "utf8",
  }).trim();
  return { boards, sdk_version, revision };
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const { boards, sdk_version, revision } = await readBoardCatalog();
  if (process.argv[2] === "--default-example") {
    const id = process.argv[3];
    const board = boards[id];
    if (!board) throw Error("Unknown board: " + id);
    const examples = describeExamples(id, board.examples, revision);
    console.log(JSON.stringify(examples.find(e => e.id === board.default_example)));
  } else {
    console.log(`Validated ${Object.keys(boards).length} boards at SDK ${sdk_version}`);
  }
}
