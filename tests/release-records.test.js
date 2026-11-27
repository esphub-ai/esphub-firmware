import assert from "node:assert/strict";
import test from "node:test";
import postgres from "postgres";
import { releaseTag } from "../shared/firmware-releases.js";
import { advanceLatest, assetRows, recordAssets } from "../scripts/release-records.mjs";

test("builds one immutable file row per asset", () => {
  const tag = releaseTag("app", "m5stack/stopwatch", "2.0.41");
  assert.deepEqual(
    assetRows({
      board: "m5stack/stopwatch",
      kind: "app",
      version: "2.0.41",
      tag,
      files: { app: { name: "esphub-m5stack_stopwatch-app-2.0.41.bin" } },
    }),
    [{
      tag,
      asset: "app",
      board: "m5stack/stopwatch",
      kind: "app",
      version: "2.0.41",
      object_key: `releases/${tag}/esphub-m5stack_stopwatch-app-2.0.41.bin`,
      size: null,
    }],
  );
});

// Requires a migrated EspHub database. Changes are rolled back.
test(
  "records files once and advances the latest release only to newer versions",
  { skip: !process.env.TEST_DATABASE_URL },
  async () => {
    const sql = postgres(process.env.TEST_DATABASE_URL, { max: 1, prepare: false });
    const rollback = Symbol("rollback");
    try {
      await sql.begin(async (tx) => {
        const board = "records-test/" + crypto.randomUUID().slice(0, 8);
        const release = (version) => ({ board, kind: "app", version, tag: releaseTag("app", board, version) });
        const publish = async (version, size) => {
          const r = release(version);
          await recordAssets(tx, assetRows({ ...r, files: { app: { name: `app-${version}.bin`, size } } }));
          await advanceLatest(tx, r);
        };
        await publish("2.0.2", 2);
        await publish("2.0.3", 3);
        await publish("2.0.2", 99);
        const [latest] = await tx`SELECT version, tag FROM esphub.firmware_releases WHERE board = ${board} AND kind = 'app'`;
        assert.deepEqual(latest, { version: "2.0.3", tag: release("2.0.3").tag });
        const files = await tx`SELECT tag, size FROM esphub.release_assets WHERE board = ${board} ORDER BY tag`;
        assert.deepEqual(files.map(({ size }) => size), ["2", "3"]);
        throw rollback;
      });
    } catch (error) {
      if (error !== rollback) throw error;
    } finally {
      await sql.end();
    }
  },
);
