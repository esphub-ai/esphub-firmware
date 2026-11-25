import { execFileSync } from "node:child_process";
import { readdir, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
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
        preview = false,
      } = JSON.parse(await file("board.json"));
      if (
        ![name, model, chip, bootloader].every(
          (v) => typeof v === "string" && v.length,
        )
      )
        throw Error("Invalid metadata for " + id);
      if (!Number.isInteger(flash_mb) || flash_mb < 8)
        throw Error("EspHub requires at least 8 MiB Flash: " + id);
      const tasks = JSON.parse(await file("ci.json"));
      const runtime = Object.hasOwn(tasks, "native") ? "native" : "micropython";
      boards[id] = {
        name,
        model,
        chip,
        flash_mb,
        bootloader,
        ...(preview ? { preview: true } : {}),
        has_setup: Object.hasOwn(tasks, "setup"),
        directory: `firmware/board/${id}`,
        runtime,
        ...(runtime === "micropython" && { hello: await file("main.py") }),
      };
    }
  }
  if (!Object.keys(boards).length) throw Error("Empty board catalog");
  const header = await readFile(
    new URL("firmware/components/okou_agent/include/okou_agent.h", repository),
    "utf8",
  );
  const sdk_version = header.match(
    /^#define OKOU_SDK_VERSION "([^"]+)"$/m,
  )?.[1];
  if (!sdk_version) throw Error("Missing EspHub SDK version");
  const upstream = JSON.parse(
    await readFile(new URL("firmware/pyrt/upstream.json", repository), "utf8"),
  );
  const revision = execFileSync("git", ["rev-parse", "HEAD"], {
    cwd: repository,
    encoding: "utf8",
  }).trim();
  return { boards, sdk_version, upstream, revision };
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const { boards, sdk_version } = await readBoardCatalog();
  await writeFile(
    new URL("shared/boards.generated.json", repository),
    JSON.stringify(boards) + "\n",
  );
  await writeFile(
    new URL("shared/sdk.generated.json", repository),
    JSON.stringify({ version: sdk_version }) + "\n",
  );
}
