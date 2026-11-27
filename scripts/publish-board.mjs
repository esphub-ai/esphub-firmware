import { readFile } from "node:fs/promises";
import { join } from "node:path";
import postgres from "postgres";
import { appAssetName, releaseTag } from "../shared/firmware-releases.js";
import { boardFileStem } from "../shared/board-id.js";
import { advanceLatest, assetRows, recordAssets } from "./release-records.mjs";
import { publishObject, r2Client, sha256 } from "./r2-objects.mjs";

const profile = JSON.parse(process.env.BOARD_PROFILE);
const { board, jobs } = profile;
const source = process.env.RELEASE_SOURCE_SHA;
const endpoint = process.env.FIRMWARE_R2_ENDPOINT;
const database = process.env.FIRMWARE_DATABASE_URL;
const stem = boardFileStem(board);

if (!/^[a-f0-9]{40}$/.test(source ?? "") || jobs?.length !== 1 || jobs[0].kind !== "app") {
  throw Error("A board release publishes exactly its app from a trusted source");
}
const [{ version, fingerprint }] = jobs;
if (!/^[0-9]+\.[0-9]+\.[0-9]+$/.test(version) || !/^[a-f0-9]{64}$/.test(fingerprint)) {
  throw Error("Invalid release version or fingerprint");
}

// The default app is published; optional examples in the artifact are build checks only.
const tag = releaseTag("app", board, version);
const name = appAssetName(board, version);
const path = join("assets", `${stem}.app`, name);
const bytes = await readFile(path);
const digest = sha256(bytes);
if (!bytes.length || bytes[0] !== 0xe9) throw Error("Invalid app image: " + name);
const checksum = await readFile(path.replace(/\.bin$/, ".sha256"), "utf8");
if (checksum.trim() !== `${digest}  ${name}`) throw Error("App image checksum mismatch: " + name);
const release = { board, kind: "app", version, tag, files: { app: { name, size: bytes.length } } };

if (!endpoint || !database) throw Error("Missing firmware publishing credentials");
const sql = postgres(database, { max: 1, prepare: false, ssl: "require" });
try {
  await publishObject(r2Client(), endpoint, `releases/${tag}/${name}`, bytes, digest);
  await sql.begin(async (tx) => {
    await recordAssets(tx, assetRows(release));
    await advanceLatest(tx, release);
  });
} finally {
  await sql.end();
}
console.log(`${board}: ${tag}`);
