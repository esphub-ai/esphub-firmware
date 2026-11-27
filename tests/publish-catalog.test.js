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

test("builds the example and source links the Hub serves", () => {
  const tree = "https://github.com/esphub-ai/esphub-firmware/tree/" + revision;
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
          "https://github.com/esphub-ai/esphub-firmware/blob/" + revision + "/docs/build-examples.md",
        build_command: "python3 scripts/build-example.py m5stack/stopwatch --example hello",
      }],
    },
  );
});

// Requires an EspHub database migrated through esphub migration 026. Changes are rolled back.
test(
  "publishes current board examples and keeps retired boards on their pinned sources",
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
        const retired = boardLinks("test/retired", profile(), old);
        await tx`UPDATE esphub.board_sources SET is_current = false WHERE is_current`;
        await tx`INSERT INTO esphub.board_sources (revision, sdk_version, is_current, sdk_source_url)
          VALUES (${old}, '7.0.0', true, ${sdkSourceUrl(old)})`;
        await tx`INSERT INTO esphub.board_examples
          (board, source_revision, default_example, published_examples, bootloader_source_url)
          VALUES ('test/retired', ${old}, 'hello',
            ${tx.json(retired.published_examples)}, ${retired.bootloader_source_url})`;
        const links = async (id) => {
          const [row] = await tx`SELECT b.source_revision, b.default_example, b.published_examples, b.bootloader_source_url, s.sdk_source_url
            FROM esphub.board_examples b JOIN esphub.board_sources s ON s.revision = b.source_revision WHERE b.board = ${id}`;
          return row;
        };
        const catalog = { boards: { "test/current": profile("test/current") }, revision, sdk_version: "8.0.0" };

        assert.equal(await publishCatalog(tx, { ...catalog, revision: old }), 0);
        assert.equal(await publishCatalog(tx, catalog), 1);
        assert.deepEqual(await links("test/current"), {
          source_revision: revision,
          default_example: "hello",
          sdk_source_url: sdkSourceUrl(revision),
          ...boardLinks("test/current", profile(), revision),
        });
        assert.deepEqual(await links("test/retired"), {
          source_revision: old,
          default_example: "hello",
          sdk_source_url: sdkSourceUrl(old),
          ...retired,
        });
        assert.equal(await publishCatalog(tx, catalog), 0);
        throw rollback;
      });
    } catch (error) {
      if (error !== rollback) throw error;
    } finally {
      await sql.end();
    }
  },
);
