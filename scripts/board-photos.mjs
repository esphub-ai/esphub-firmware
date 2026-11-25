import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { lstat, readFile } from "node:fs/promises";
import { AwsClient } from "aws4fetch";
import { boardId } from "../shared/board-id.js";

const publish = process.argv.includes("--publish");
const { FIRMWARE_R2_ENDPOINT, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY } =
  process.env;
if (
  publish &&
  !(FIRMWARE_R2_ENDPOINT && AWS_ACCESS_KEY_ID && AWS_SECRET_ACCESS_KEY)
)
  throw Error("Missing firmware R2 credentials");
const client =
  publish &&
  new AwsClient({
    accessKeyId: AWS_ACCESS_KEY_ID,
    secretAccessKey: AWS_SECRET_ACCESS_KEY,
    service: "s3",
    region: "auto",
  });

const paths = execFileSync("git", ["ls-files", "-z", "--", "static/boards"], {
  encoding: "utf8",
})
  .split("\0")
  .filter(Boolean);
let uploaded = 0;
for (const path of paths) {
  const match = /^static\/boards\/([^/]+)\/([^/]+)\.webp$/.exec(path);
  if (!match || !(await lstat(path)).isFile())
    throw Error("Unexpected board photo path: " + path);
  const id = boardId(match[1], match[2]);
  const bytes = await readFile(path);
  if (
    bytes.length < 20 ||
    bytes.toString("ascii", 0, 4) !== "RIFF" ||
    bytes.readUInt32LE(4) !== bytes.length - 8 ||
    bytes.toString("ascii", 8, 12) !== "WEBP" ||
    !["VP8 ", "VP8L", "VP8X"].includes(bytes.toString("ascii", 12, 16))
  )
    throw Error("Invalid WebP: " + path);
  if (!publish) continue;

  const url = new URL(
    `esphub-firmware/boards/${id}.webp`,
    FIRMWARE_R2_ENDPOINT,
  );
  // Single-part R2 objects normally have an MD5 ETag; compare bytes for other ETags.
  const etag = '"' + createHash("md5").update(bytes).digest("hex") + '"';
  const current = await client.fetch(url, {
    headers: { "If-None-Match": etag },
  });
  if (current.status === 304) continue;
  if (!current.ok && current.status !== 404)
    throw Error(`R2 read ${id}: HTTP ${current.status}`);
  if (current.ok && bytes.equals(Buffer.from(await current.arrayBuffer())))
    continue;

  const saved = await client.fetch(url, {
    method: "PUT",
    body: bytes,
    headers: {
      ...(current.ok
        ? { "If-Match": current.headers.get("ETag") }
        : { "If-None-Match": "*" }),
      "Content-Type": "image/webp",
    },
  });
  if (!saved.ok) throw Error(`R2 upload ${id}: HTTP ${saved.status}`);
  const check = await client.fetch(url);
  if (!check.ok || !bytes.equals(Buffer.from(await check.arrayBuffer())))
    throw Error("R2 verification failed: " + id);
  uploaded++;
}
console.log(
  `Validated ${paths.length} board photos.` +
    (publish ? ` Uploaded ${uploaded}.` : ""),
);
