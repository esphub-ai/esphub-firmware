import { readdir, readFile, writeFile } from "node:fs/promises";
import { boardId } from "../shared/board-id.js";

const root = new URL("../firmware/board/", import.meta.url);
const boards = {};
for (const vendor of await readdir(root, { withFileTypes: true })) {
  if (!vendor.isDirectory()) continue;
  const vendorRoot = new URL(vendor.name + "/", root);
  for (const modelDir of await readdir(vendorRoot, { withFileTypes: true })) {
    if (!modelDir.isDirectory()) continue;
    const id = boardId(vendor.name, modelDir.name);
    const directory = new URL(modelDir.name + "/", vendorRoot);
    const file = (name) => readFile(new URL(name, directory), "utf8");
    const {
      name,
      model,
      chip,
      flash_mb,
      bootloader,
      preview = false,
    } = JSON.parse(await file("board.json"));
    if (![name, model, chip].every((v) => typeof v === "string" && v.length))
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
await writeFile(
  new URL("../shared/boards.generated.json", import.meta.url),
  JSON.stringify(boards) + "\n",
);

const sdkHeader = await readFile(
  new URL(
    "../firmware/components/okou_agent/include/okou_agent.h",
    import.meta.url,
  ),
  "utf8",
);
const sdkVersion = sdkHeader.match(
  /^#define OKOU_SDK_VERSION "([^"]+)"$/m,
)?.[1];
if (!sdkVersion) throw Error("Missing EspHub SDK version");
await writeFile(
  new URL("../shared/sdk.generated.json", import.meta.url),
  JSON.stringify({ version: sdkVersion }) + "\n",
);
