import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import postgres from "postgres";
import { describeExamples } from "../shared/board-examples.js";
import { readBoardCatalog } from "./board-catalog.mjs";

const tree = (revision) =>
  "https://github.com/okou-ai/esphub-firmware/tree/" + revision;

// The Hub returns these links as stored, so it needs no knowledge of this repository's layout.
export const sdkSourceUrl = (revision) =>
  tree(revision) + "/firmware/components/eh_agent";

export function boardLinks(id, { bootloader, examples }, revision) {
  return {
    published_examples: describeExamples(id, examples, revision),
    bootloader_source_url: tree(revision) + "/firmware/bootloader/" + bootloader,
  };
}

// Publish the board directory at this revision. Retired boards keep their pinned sources
// for existing devices and versioned downloads.
export async function publishCatalog(tx, { boards, revision, sdk_version }) {
  await tx`SELECT pg_advisory_xact_lock(hashtext('esphub.board_catalog'))`;
  const [current] =
    await tx`SELECT revision FROM esphub.board_sources WHERE is_current`;
  if (current?.revision === revision) return 0;
  const rows = Object.entries(boards).map(([id, profile]) => ({
    id,
    ...profile,
    ...boardLinks(id, profile, revision),
  }));
  await tx`UPDATE esphub.board_sources SET is_current = false WHERE is_current`;
  await tx`INSERT INTO esphub.board_sources (revision, sdk_version, is_current, sdk_source_url)
    VALUES (${revision}, ${sdk_version}, true, ${sdkSourceUrl(revision)})
    ON CONFLICT (revision) DO UPDATE SET is_current = true, sdk_source_url = excluded.sdk_source_url`;
  await tx`INSERT INTO esphub.boards
      (id, source_revision, name, model, chip, flash_mb, bootloader, default_example, preview,
       published_examples, bootloader_source_url)
    SELECT id, ${revision}, name, model, chip, flash_mb, bootloader, default_example, coalesce(preview, false),
      published_examples, bootloader_source_url
    FROM jsonb_to_recordset(${tx.json(rows)}::jsonb) AS b(id text, name text, model text, chip text,
      flash_mb int, bootloader text, default_example text, preview boolean,
      published_examples jsonb, bootloader_source_url text)
    ON CONFLICT (id) DO UPDATE SET
      source_revision = excluded.source_revision, name = excluded.name, model = excluded.model, chip = excluded.chip,
      flash_mb = excluded.flash_mb, bootloader = excluded.bootloader, default_example = excluded.default_example,
      preview = excluded.preview, published_examples = excluded.published_examples,
      bootloader_source_url = excluded.bootloader_source_url`;
  await tx`DELETE FROM esphub.board_sources s WHERE NOT s.is_current
    AND NOT EXISTS (SELECT 1 FROM esphub.boards b WHERE b.source_revision = s.revision)`;
  return rows.length;
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const catalog = await readBoardCatalog();
  const database = process.env.FIRMWARE_DATABASE_URL;
  if (!database) throw Error("Missing FIRMWARE_DATABASE_URL");
  const sql = postgres(database, { max: 1, prepare: false });
  try {
    const published = await sql.begin((tx) => publishCatalog(tx, catalog));
    if (published)
      console.log(`Published ${published} board profiles at ${catalog.revision}`);
  } finally {
    await sql.end();
  }
}
