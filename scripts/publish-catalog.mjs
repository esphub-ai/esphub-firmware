import postgres from "postgres";
import { readBoardCatalog } from "./board-catalog.mjs";

const { boards, revision, sdk_version, upstream } = await readBoardCatalog();
const rows = Object.entries(boards).map(([id, profile]) => ({
  id,
  ...profile,
}));
const database = process.env.FIRMWARE_DATABASE_URL;
if (!database) throw Error("Missing FIRMWARE_DATABASE_URL");
const sql = postgres(database, { max: 1, prepare: false });
try {
  await sql.begin(async (tx) => {
    await tx`SELECT pg_advisory_xact_lock(hashtext('esphub.board_catalog'))`;
    const [current] =
      await tx`SELECT revision FROM esphub.board_sources WHERE is_current`;
    if (
      current &&
      (current.revision === revision || process.argv.includes("--if-empty"))
    )
      return;

    await tx`UPDATE esphub.board_sources SET is_current = false WHERE is_current`;
    await tx`INSERT INTO esphub.board_sources (revision, sdk_version, upstream_repository, upstream_ref, is_current)
      VALUES (${revision}, ${sdk_version}, ${upstream.repository}, ${upstream.ref}, true)
      ON CONFLICT (revision) DO UPDATE SET is_current = true`;
    await tx`INSERT INTO esphub.boards
        (id, source_revision, name, model, chip, flash_mb, bootloader, runtime, preview, has_setup, hello)
      SELECT id, ${revision}, name, model, chip, flash_mb, bootloader, runtime, coalesce(preview, false), has_setup, hello
      FROM jsonb_to_recordset(${tx.json(rows)}::jsonb) AS b(id text, name text, model text, chip text,
        flash_mb int, bootloader text, runtime text, preview boolean, has_setup boolean, hello text)
      ON CONFLICT (id) DO UPDATE SET
        source_revision = excluded.source_revision, name = excluded.name, model = excluded.model, chip = excluded.chip,
        flash_mb = excluded.flash_mb, bootloader = excluded.bootloader, runtime = excluded.runtime,
        preview = excluded.preview, has_setup = excluded.has_setup, hello = excluded.hello`;
    // Retired boards keep their pinned sources for existing devices and versioned downloads.
    await tx`DELETE FROM esphub.board_sources s WHERE NOT s.is_current
      AND NOT EXISTS (SELECT 1 FROM esphub.boards b WHERE b.source_revision = s.revision)`;
    console.log(`Published ${rows.length} board profiles at ${revision}`);
  });
} finally {
  await sql.end();
}
