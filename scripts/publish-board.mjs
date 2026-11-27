import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import postgres from "postgres";
import { AwsClient } from "aws4fetch";
import { assetNames, releaseTag } from "../shared/firmware-releases.js";
import { boardFileStem } from "../shared/board-id.js";
import { advanceLatest, assetRows, recordAssets } from "./release-records.mjs";

const profile = JSON.parse(process.env.BOARD_PROFILE);
const { board, chip, jobs } = profile;
const source = process.env.RELEASE_SOURCE_SHA;
const endpoint = process.env.FIRMWARE_R2_ENDPOINT;
const database = process.env.FIRMWARE_DATABASE_URL;
const bucket = "esphub-firmware";
const stem = boardFileStem(board);

if (!/^[a-f0-9]{40}$/.test(source ?? "") || !jobs.length) {
  throw Error("Missing release source or build jobs");
}

const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");

async function readRelease({ kind, version, fingerprint }) {
  if (
    !/^[0-9]+\.[0-9]+\.[0-9]+$/.test(version) ||
    !/^[a-f0-9]{64}$/.test(fingerprint)
  ) {
    throw Error("Invalid release version or fingerprint");
  }
  const tag = releaseTag(kind, board, version);
  const names = assetNames(board, chip, kind, version);
  const files = Object.fromEntries(
    await Promise.all(
      Object.entries(names).map(async ([asset, name]) => {
        const path = join("assets", `${stem}.${kind}`, name);
        const bytes = await readFile(path);
        if (!bytes.length) throw Error("Empty asset: " + name);
        return [
          asset,
          {
            path,
            name,
            size: bytes.length,
            sha256: hash(bytes),
            bytes: name.endsWith(".json") ? bytes : undefined,
          },
        ];
      }),
    ),
  );
  // The bootloader build manifest describes the OTA image but is not published.
  const manifestBytes = kind === "bootloader"
    ? await readFile(join("assets", `${stem}.${kind}`, `esphub-${stem}-${chip}-${version}.json`))
    : files["setup-manifest"]?.bytes;
  const manifest = manifestBytes && JSON.parse(manifestBytes);
  if (
    manifest &&
    (manifest.board !== board ||
      manifest.chip !== chip ||
      manifest.version !== version)
  ) {
    throw Error("Manifest identity mismatch: " + tag);
  }
  if (kind === "bootloader") {
    if (
      manifest.ota?.file !== files.ota.name ||
      manifest.ota.sha256 !== files.ota.sha256 ||
      manifest.ota.size !== files.ota.size
    ) {
      throw Error("Bootloader manifest checksum or length mismatch");
    }
  } else if (kind === "setup") {
    if (
      manifest.file !== files.setup.name ||
      manifest.sha256 !== files.setup.sha256
    ) {
      throw Error("USB kit checksum mismatch");
    }
  } else {
    const image = files[kind];
    const checksum = await readFile(
      image.path.replace(/\.bin$/, ".sha256"),
      "utf8",
    );
    if (checksum.trim() !== `${image.sha256}  ${image.name}`) {
      throw Error("App image checksum mismatch: " + image.name);
    }
  }
  return { board, kind, version, tag, files };
}

async function publishObject(aws, key, bytes, sha256) {
  const type = key.endsWith(".json")
    ? "application/json"
    : key.endsWith(".zip")
      ? "application/zip"
      : "application/octet-stream";
  const url = new URL(bucket + "/" + key, endpoint);
  const put = await aws.fetch(url, {
    method: "PUT",
    headers: {
      "If-None-Match": "*",
      "Content-Type": type,
      "x-amz-meta-sha256": sha256,
    },
    body: bytes,
  });
  if (!put.ok && put.status !== 412) {
    throw Error(`R2 upload failed (HTTP ${put.status}): ${key}`);
  }
  const stored = await aws.fetch(url, { method: "HEAD" });
  const length = stored.headers.get("Content-Length");
  if (!stored.ok || stored.headers.get("x-amz-meta-sha256") !== sha256) {
    throw Error("R2 checksum metadata does not match the build: " + key);
  }
  if (length === null) {
    // R2 can omit Content-Length when the response is compressed.
    const object = await aws.fetch(url);
    const received = Buffer.from(await object.arrayBuffer());
    if (
      !object.ok ||
      received.length !== bytes.length ||
      hash(received) !== sha256
    ) {
      throw Error("R2 object does not match the build: " + key);
    }
  } else if (Number(length) !== bytes.length) {
    throw Error("R2 object size does not match the build: " + key);
  }
}

const releases = await Promise.all(jobs.map(readRelease));
const byKind = Object.fromEntries(releases.map((release) => [release.kind, release]));
if (!byKind.bootloader || !byKind.app || !byKind.setup)
  throw Error("A board release needs its bootloader, app and setup kit");
const kitManifest = JSON.parse(byKind.setup.files["setup-manifest"].bytes);
if (kitManifest.app_tag !== byKind.app.tag || kitManifest.app_sha256 !== byKind.app.files.app.sha256 ||
    kitManifest.app_size !== byKind.app.files.app.size ||
    !kitManifest.example?.source_url?.includes("/tree/" + source + "/"))
  throw Error("Setup kit and published app must use the same image and pinned example");
if (!endpoint || !database)
  throw Error("Missing firmware publishing credentials");
const aws = new AwsClient({
  accessKeyId: process.env.AWS_ACCESS_KEY_ID,
  secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY,
  service: "s3",
  region: "auto",
});
const sql = postgres(database, { max: 1, prepare: false, ssl: "require" });
try {
  for (const release of releases) {
    const prefix = `releases/${release.tag}/`;
    for (const { path, name, sha256 } of Object.values(release.files)) {
      await publishObject(aws, prefix + name, await readFile(path), sha256);
    }
  }
  await sql.begin(async (tx) => {
    for (const release of releases) {
      await recordAssets(tx, assetRows(release));
      await advanceLatest(tx, release);
    }
  });
} finally {
  await sql.end();
}
console.log(`${board}: ${releases.map(({ tag }) => tag).join(", ")}`);
