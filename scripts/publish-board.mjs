import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { readFile, writeFile, mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import postgres from "postgres";
import { assetNames, releaseTag } from "../shared/firmware-releases.js";
import { boardFileStem } from "../shared/board-id.js";

const profile = JSON.parse(process.env.BOARD_PROFILE);
const { board, chip, jobs } = profile;
const source = process.env.GITHUB_SHA;
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
  const manifestFile = files.manifest ?? files["setup-manifest"];
  const manifest = manifestFile && JSON.parse(manifestFile.bytes);
  if (
    manifest &&
    (manifest.board !== board ||
      manifest.chip !== chip ||
      manifest.version !== version)
  ) {
    throw Error("Manifest identity mismatch: " + tag);
  }
  if (kind === "bootloader") {
    for (const asset of ["ota", "usb"]) {
      if (
        manifest[asset]?.file !== files[asset].name ||
        manifest[asset]?.sha256 !== files[asset].sha256 ||
        (asset === "ota" && manifest.ota.size !== files.ota.size)
      ) {
        throw Error(
          "Bootloader manifest checksum or length mismatch: " + asset,
        );
      }
    }
  } else if (kind === "setup" && manifest.sha256 !== files.setup.sha256) {
    throw Error("USB kit checksum mismatch");
  }
  return {
    board,
    kind,
    version,
    fingerprint,
    source,
    tag,
    files,
    manifest: kind === "bootloader" ? manifest : null,
  };
}

function s3(operation, ...args) {
  return execFileSync(
    "aws",
    [
      "s3api",
      operation,
      "--endpoint-url",
      endpoint,
      "--region",
      "auto",
      "--bucket",
      bucket,
      ...args,
    ],
    { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
  );
}

function publishObject(key, path, size, sha256) {
  const type = key.endsWith(".json")
    ? "application/json"
    : key.endsWith(".zip")
      ? "application/zip"
      : "application/octet-stream";
  try {
    s3(
      "put-object",
      "--key",
      key,
      "--body",
      path,
      "--content-type",
      type,
      "--if-none-match",
      "*",
      "--metadata",
      "sha256=" + sha256,
    );
  } catch {
    // A retry can reuse the exact bytes previously uploaded to this immutable key.
  }
  let remote;
  try {
    remote = JSON.parse(s3("head-object", "--key", key));
  } catch {
    throw Error("Unable to read published R2 object: " + key);
  }
  if (remote.ContentLength !== size || remote.Metadata?.sha256 !== sha256) {
    throw Error("R2 object does not match the build: " + key);
  }
}

const releases = await Promise.all(jobs.map(readRelease));
if (!process.argv.includes("--check")) {
  if (!endpoint || !database)
    throw Error("Missing firmware publishing credentials");
  const directory = await mkdtemp(join(tmpdir(), "esphub-firmware-"));
  const sql = postgres(database, { max: 1, prepare: false, ssl: "require" });
  try {
    for (const release of releases) {
      const prefix = `releases/${release.tag}/`;
      for (const { path, name, size, sha256 } of Object.values(release.files)) {
        publishObject(prefix + name, path, size, sha256);
      }
    }
    for (const { tag, fingerprint, files } of releases) {
      const marker = Buffer.from(
        JSON.stringify({
          tag,
          fingerprint,
          source,
          assets: Object.values(files).map(({ name, size, sha256 }) => ({
            name,
            size,
            sha256,
          })),
        }),
      );
      const path = join(directory, "published.json");
      await writeFile(path, marker);
      publishObject(
        `releases/${tag}/published.json`,
        path,
        marker.length,
        hash(marker),
      );
    }
    await sql.begin(async (tx) => {
      for (const { kind, version, manifest } of releases) {
        await tx`INSERT INTO esphub.firmware_releases AS current (board, kind, version, manifest)
          VALUES (${board}, ${kind}, ${version}, ${manifest ? tx.json(manifest) : null})
          ON CONFLICT (board, kind) DO UPDATE SET version = excluded.version, manifest = excluded.manifest
          WHERE string_to_array(excluded.version, '.')::numeric[] > string_to_array(current.version, '.')::numeric[]`;
      }
    });
  } finally {
    await sql.end();
    await rm(directory, { recursive: true, force: true });
  }
}
console.log(`${board}: ${releases.map(({ tag }) => tag).join(", ")}`);
