import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import postgres from "postgres";
import { bootloaderRow, recordBootloader } from "../scripts/publish-bootloader.mjs";

const id = "esp32s3-16mb", version = "2.0.42";
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");

function set(launcherId = id) {
  const launcher = Buffer.alloc(4096, 0xff);
  launcher[0] = 0xe9;
  launcher.write(`ESPHUB2/bootloader/${launcherId}\0bootloader-ota-v2\0`, 288);
  const files = {
    bootloader: Buffer.from([0xe9, 1, 2, 3]),
    partition_table: Buffer.from([0xaa, 0x50, 1, 2]),
    launcher,
  };
  const names = { bootloader: "bootloader.bin", partition_table: "partition-table.bin", launcher: "launcher.bin" };
  const offsets = { bootloader: 0, partition_table: 0x8000, launcher: 0x20000 };
  const descriptor = {
    id, version, chip: "esp32s3", flash_mode: "dio", flash_size: "16MB", flash_freq: "80m",
    app_offset: 0x2a0000, nvs_offset: 0x9000, nvs_size: 0x6000,
    files: Object.fromEntries(Object.entries(files).map(([key, bytes]) => [key, {
      name: names[key], offset: offsets[key], size: bytes.length, sha256: sha256(bytes),
    }])),
  };
  return { descriptor, files };
}

test("a bootloader set row records each file's flash offset, R2 key and digest", () => {
  const { descriptor, files } = set();
  const row = bootloaderRow(descriptor, files, id);
  assert.deepEqual(row, {
    id, version, chip: "esp32s3", flash_mode: "dio", flash_size: "16MB", flash_freq: "80m",
    app_offset: 0x2a0000, nvs_offset: 0x9000, nvs_size: 0x6000,
    bootloader_offset: 0, bootloader_key: `bootloaders/${id}/${version}/bootloader.bin`,
    bootloader_sha256: sha256(files.bootloader),
    partition_table_offset: 0x8000, partition_table_key: `bootloaders/${id}/${version}/partition-table.bin`,
    partition_table_sha256: sha256(files.partition_table),
    launcher_offset: 0x20000, launcher_key: `bootloaders/${id}/${version}/launcher.bin`,
    launcher_sha256: sha256(files.launcher), launcher_size: 4096,
  });
  assert.throws(() => bootloaderRow(descriptor, files, "esp32s3-8mb"), /identity/);
  assert.throws(() => bootloaderRow(descriptor, { ...files, bootloader: Buffer.from([0xe9, 9]) }, id), /descriptor/);
  const other = set("esp32s3-8mb");
  assert.throws(() => bootloaderRow(other.descriptor, other.files, id), /different bootloader ID/);
  assert.throws(() => bootloaderRow({ ...descriptor, chip: "esp32c6" }, files, id), /chip/);
});

// Requires an EspHub database migrated through esphub migration 024. Changes are rolled back.
test(
  "a bootloader set is recorded once and a different set cannot reuse its version",
  { skip: !process.env.TEST_DATABASE_URL },
  async () => {
    const sql = postgres(process.env.TEST_DATABASE_URL, { max: 1, prepare: false });
    const rollback = Symbol("rollback");
    try {
      await sql.begin(async (tx) => {
        const { descriptor, files } = set();
        const row = bootloaderRow({ ...descriptor, version: "0.0.1" }, files, id);
        await recordBootloader(tx, row);
        await recordBootloader(tx, row);
        const [{ count }] = await tx`SELECT count(*)::int AS count FROM esphub.bootloaders WHERE id = ${id} AND version = '0.0.1'`;
        assert.equal(count, 1);
        await assert.rejects(recordBootloader(tx, { ...row, launcher_sha256: "0".repeat(64) }), /already uses/);
        throw rollback;
      });
    } catch (error) {
      if (error !== rollback) throw error;
    } finally {
      await sql.end();
    }
  },
);
