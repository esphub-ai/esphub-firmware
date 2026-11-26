import assert from "node:assert/strict";
import test from "node:test";
import postgres from "postgres";
import { boardLinks, publishCatalog, sdkSourceUrl } from "../scripts/publish-catalog.mjs";

const revision = "a".repeat(40);
const example = {
  id: "hello",
  description: "Print hello",
  entrypoint: "examples/hello/main/main.c",
  upstream: "https://example.com/hello",
};

test("records the source links the Hub previously derived", () => {
  const tree = "https://github.com/okou-ai/esphub-firmware/tree/" + revision;
  assert.equal(sdkSourceUrl(revision), tree + "/firmware/components/eh_agent");
  assert.deepEqual(
    boardLinks("m5stack/stopwatch", { bootloader: "esp32s3-8mb", examples: [example] }, revision),
    {
      bootloader_source_url: tree + "/firmware/bootloader/esp32s3-8mb",
      published_examples: [{
        id: "hello",
        description: "Print hello",
        upstream: "https://example.com/hello",
        source_url: tree + "/firmware/board/m5stack/stopwatch/examples/hello",
        entrypoint: "main/main.c",
        build_guide_url:
          "https://github.com/okou-ai/esphub-firmware/blob/" + revision + "/docs/build-examples.md",
        build_command: "python3 scripts/build-example.py m5stack/stopwatch --example hello",
      }],
    },
  );
});

// Requires an EspHub database migrated through esphub migration 020. Changes are rolled back.
test(
  "publishes links for current boards and backfills retired boards",
  { skip: !process.env.TEST_DATABASE_URL },
  async () => {
    const sql = postgres(process.env.TEST_DATABASE_URL, { max: 1, prepare: false });
    const rollback = Symbol("rollback");
    try {
      await sql.begin(async (tx) => {
        const old = "b".repeat(40);
        const profile = (id) => ({
          name: id,
          model: id,
          chip: "esp32s3",
          flash_mb: 16,
          bootloader: "esp32s3-16mb",
          default_example: "hello",
          examples: [example],
        });
        await tx`UPDATE esphub.board_sources SET is_current = false WHERE is_current`;
        // A board retired before links were recorded keeps its older pinned revision.
        await tx`INSERT INTO esphub.board_sources (revision, sdk_version, is_current) VALUES (${old}, '7.0.0', true)`;
        await tx`INSERT INTO esphub.boards
          (id, source_revision, name, model, chip, flash_mb, bootloader, default_example, examples, preview)
          VALUES ('test/retired', ${old}, 'Retired', 'Retired', 'esp32s3', 16, 'esp32s3-16mb', 'hello',
            ${tx.json([example])}, false)`;
        const links = async (id) => {
          const [row] = await tx`SELECT b.source_revision, b.published_examples, b.bootloader_source_url, s.sdk_source_url
            FROM esphub.boards b JOIN esphub.board_sources s ON s.revision = b.source_revision WHERE b.id = ${id}`;
          return row;
        };
        const catalog = { boards: { "test/current": profile("test/current") }, revision, sdk_version: "8.0.0" };

        // An unchanged current revision still backfills existing rows.
        assert.deepEqual(
          await publishCatalog(tx, { ...catalog, revision: old }),
          { published: 0, backfilled: { boards: 1, sources: 1 } },
        );
        assert.deepEqual(await links("test/retired"), {
          source_revision: old,
          sdk_source_url: sdkSourceUrl(old),
          ...boardLinks("test/retired", profile(), old),
        });

        assert.deepEqual(await publishCatalog(tx, catalog, { ifEmpty: true }), {
          published: 0,
          backfilled: { boards: 0, sources: 0 },
        });
        assert.equal(await links("test/current"), undefined);

        assert.deepEqual(await publishCatalog(tx, catalog), {
          published: 1,
          backfilled: { boards: 0, sources: 0 },
        });
        assert.deepEqual(await links("test/current"), {
          source_revision: revision,
          sdk_source_url: sdkSourceUrl(revision),
          ...boardLinks("test/current", profile(), revision),
        });
        assert.equal((await links("test/retired")).source_revision, old);
        assert.deepEqual(await publishCatalog(tx, catalog), {
          published: 0,
          backfilled: { boards: 0, sources: 0 },
        });
        throw rollback;
      });
    } catch (error) {
      if (error !== rollback) throw error;
    } finally {
      await sql.end();
    }
  },
);
