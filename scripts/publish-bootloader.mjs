// Publish one bootloader set: the ESP-IDF second-stage bootloader, the partition table and
// the EspHub Launcher shared by every board with this bootloader ID.
import { readFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import postgres from "postgres";
import { publishObject, r2Client, sha256 } from "./r2-objects.mjs";

const fileKeys = ["bootloader", "partition_table", "launcher"];

export const objectKey = (id, version, name) => `bootloaders/${id}/${version}/${name}`;

// Check a build descriptor against its files and return the Neon row it publishes.
export function bootloaderRow(descriptor, files, expectedId) {
  const { id, version } = descriptor;
  if (id !== expectedId || !/^[a-z0-9]+-\d+mb(?:-[a-z0-9-]+)?$/.test(id))
    throw Error("Bootloader descriptor identity mismatch");
  if (!/^[0-9]+\.[0-9]+\.[0-9]+$/.test(version)) throw Error("Invalid bootloader version");
  if (descriptor.chip !== id.split("-", 1)[0]) throw Error("Bootloader chip mismatch");
  for (const key of ["flash_mode", "flash_size", "flash_freq"])
    if (typeof descriptor[key] !== "string" || !descriptor[key]) throw Error("Missing " + key);
  const row = {
    id, version, chip: descriptor.chip,
    flash_mode: descriptor.flash_mode, flash_size: descriptor.flash_size, flash_freq: descriptor.flash_freq,
    app_offset: descriptor.app_offset, nvs_offset: descriptor.nvs_offset, nvs_size: descriptor.nvs_size,
  };
  for (const key of fileKeys) {
    const file = descriptor.files?.[key], bytes = files[key];
    if (!file || !bytes?.length || file.size !== bytes.length || file.sha256 !== sha256(bytes))
      throw Error("Bootloader file does not match its descriptor: " + key);
    row[key + "_offset"] = file.offset;
    row[key + "_key"] = objectKey(id, version, file.name);
    row[key + "_sha256"] = file.sha256;
  }
  row.launcher_size = files.launcher.length;
  for (const [key, value] of Object.entries(row))
    if (key.endsWith("_offset") || (key.endsWith("_size") && key !== "flash_size"))
      if (!Number.isSafeInteger(value) || value < 0) throw Error("Invalid " + key);
  if (files.bootloader[0] !== 0xe9 || files.launcher[0] !== 0xe9 || files.partition_table[0] !== 0xaa)
    throw Error("Unexpected bootloader set image format");
  // The Launcher accepts an OTA image only with the header of its own bootloader ID.
  const header = Buffer.from(`ESPHUB2/bootloader/${id}\0bootloader-ota-v2\0`);
  if (!Buffer.from(files.launcher.subarray(288, 288 + header.length)).equals(header))
    throw Error("Launcher image belongs to a different bootloader ID");
  if (row.app_offset <= row.launcher_offset || row.nvs_offset >= row.launcher_offset)
    throw Error("Unexpected partition layout");
  return row;
}

// Insert once. A rebuilt set with the same ID and version must be byte-identical.
export async function recordBootloader(tx, row) {
  await tx`INSERT INTO esphub.bootloaders ${tx(row)} ON CONFLICT (id, version) DO NOTHING`;
  const [stored] = await tx`SELECT * FROM esphub.bootloaders WHERE id = ${row.id} AND version = ${row.version}`;
  for (const [key, value] of Object.entries(row))
    if (String(stored[key]) !== String(value)) throw Error("A different bootloader set already uses " + row.version);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const id = process.env.BOOTLOADER;
  const endpoint = process.env.FIRMWARE_R2_ENDPOINT;
  const database = process.env.FIRMWARE_DATABASE_URL;
  const dir = join("assets", "bl-" + id);
  const descriptor = JSON.parse(await readFile(join(dir, "build.json"), "utf8"));
  const files = Object.fromEntries(await Promise.all(fileKeys.map(async key =>
    [key, await readFile(join(dir, descriptor.files[key].name))])));
  const row = bootloaderRow(descriptor, files, id);
  if (!endpoint || !database) throw Error("Missing firmware publishing credentials");
  const aws = r2Client();
  for (const key of fileKeys)
    await publishObject(aws, endpoint, row[key + "_key"], files[key], row[key + "_sha256"]);
  const sql = postgres(database, { max: 1, prepare: false, ssl: "require" });
  try {
    await sql.begin(tx => recordBootloader(tx, row));
  } finally {
    await sql.end();
  }
  console.log(`bootloader ${id} ${row.version}: ${fileKeys.map(key => row[key + "_key"]).join(", ")}`);
}
