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

export async function publishCatalog(tx, { boards, revision, sdk_version }, { ifEmpty = false } = {}) {
  await tx`SELECT pg_advisory_xact_lock(hashtext('esphub.board_catalog'))`;
  const [current] =
    await tx`SELECT revision FROM esphub.board_sources WHERE is_current`;
  let published = 0;
  if (!current || (current.revision !== revision && !ifEmpty)) {
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
        (id, source_revision, name, model, chip, flash_mb, bootloader, default_example, examples, preview,
         published_examples, bootloader_source_url)
      SELECT id, ${revision}, name, model, chip, flash_mb, bootloader, default_example, examples, coalesce(preview, false),
        published_examples, bootloader_source_url
      FROM jsonb_to_recordset(${tx.json(rows)}::jsonb) AS b(id text, name text, model text, chip text,
        flash_mb int, bootloader text, default_example text, examples jsonb, preview boolean,
        published_examples jsonb, bootloader_source_url text)
      ON CONFLICT (id) DO UPDATE SET
        source_revision = excluded.source_revision, name = excluded.name, model = excluded.model, chip = excluded.chip,
        flash_mb = excluded.flash_mb, bootloader = excluded.bootloader, default_example = excluded.default_example,
        preview = excluded.preview, examples = excluded.examples,
        published_examples = excluded.published_examples, bootloader_source_url = excluded.bootloader_source_url`;
    // Retired boards keep their pinned sources for existing devices and versioned downloads.
    await tx`DELETE FROM esphub.board_sources s WHERE NOT s.is_current
      AND NOT EXISTS (SELECT 1 FROM esphub.boards b WHERE b.source_revision = s.revision)`;
    published = rows.length;
  }

  // Fill links for rows published before the publisher recorded them, including retired
  // boards that keep an older source revision.
  const sources = (
    await tx`SELECT revision FROM esphub.board_sources WHERE sdk_source_url IS NULL`
  ).map(({ revision }) => ({ revision, sdk_source_url: sdkSourceUrl(revision) }));
  if (sources.length)
    await tx`UPDATE esphub.board_sources s SET sdk_source_url = l.sdk_source_url
      FROM jsonb_to_recordset(${tx.json(sources)}::jsonb) AS l(revision text, sdk_source_url text)
      WHERE s.revision = l.revision AND s.sdk_source_url IS NULL`;
  const links = (
    await tx`SELECT id, source_revision, bootloader, examples FROM esphub.boards
      WHERE published_examples IS NULL OR bootloader_source_url IS NULL`
  ).map((row) => ({ id: row.id, ...boardLinks(row.id, row, row.source_revision) }));
  if (links.length)
    await tx`UPDATE esphub.boards b SET published_examples = l.published_examples,
        bootloader_source_url = l.bootloader_source_url
      FROM jsonb_to_recordset(${tx.json(links)}::jsonb)
        AS l(id text, published_examples jsonb, bootloader_source_url text)
      WHERE b.id = l.id`;
  return { published, backfilled: { boards: links.length, sources: sources.length } };
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
    const { published, backfilled } = await sql.begin((tx) =>
      publishCatalog(tx, catalog, { ifEmpty: process.argv.includes("--if-empty") }),
    );
    if (published)
      console.log(`Published ${published} board profiles at ${catalog.revision}`);
    if (backfilled.boards || backfilled.sources)
      console.log(
        `Backfilled source links for ${backfilled.boards} boards and ${backfilled.sources} source revisions`,
      );
  } finally {
    await sql.end();
  }
}
