// Neon records of published releases. The Hub serves downloads from these rows,
// so it needs neither this repository's release naming nor the R2 object layout.

export function assetRows({ board, kind, version, tag, files }) {
  return Object.entries(files).map(([asset, { name, size }]) => ({
    tag,
    asset,
    board,
    kind,
    version,
    object_key: `releases/${tag}/${name}`,
    size: size ?? null,
  }));
}

// Published files are immutable, so an existing row for the same tag and asset is kept.
export async function recordAssets(tx, rows) {
  for (let i = 0; i < rows.length; i += 1000) {
    const batch = rows.slice(i, i + 1000);
    await tx`INSERT INTO esphub.release_assets
      ${tx(batch, "tag", "asset", "board", "kind", "version", "object_key", "size")}
      ON CONFLICT (tag, asset) DO NOTHING`;
  }
}

// Advance the latest release only to a newer version.
export async function advanceLatest(tx, { board, kind, version, tag }) {
  await tx`INSERT INTO esphub.firmware_releases AS current (board, kind, version, tag)
    VALUES (${board}, ${kind}, ${version}, ${tag})
    ON CONFLICT (board, kind) DO UPDATE SET version = excluded.version, tag = excluded.tag
    WHERE string_to_array(excluded.version, '.')::numeric[] > string_to_array(current.version, '.')::numeric[]`;
}
