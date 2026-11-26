// One-time index of releases published before the publisher recorded their files in Neon.
// It reads each release receipt in R2, records the files the Hub would serve for that tag,
// tags the latest releases, and verifies that every latest release is fully indexed.
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { AwsClient } from "aws4fetch";
import postgres from "postgres";
import { assetNames, releaseTag } from "../shared/firmware-releases.js";
import { assetRows, recordAssets } from "./release-records.mjs";

const bucket = "esphub-firmware";
const segment = "[a-z0-9]+(?:-[a-z0-9]+)*";
const tagPattern = new RegExp(
  `^firmware-(bootloader|app|setup)-(${segment})_(${segment})-v(\\d+\\.\\d+\\.\\d+)$`,
);
const entities = { lt: "<", gt: ">", quot: '"', apos: "'", amp: "&" };
const unescapeXml = (text) =>
  text.replace(/&(lt|gt|quot|apos|amp);/g, (_, name) => entities[name]);

// Parse one ListObjectsV2 page that used a delimiter.
export function parseListing(xml) {
  const prefixes = [
    ...xml.matchAll(/<CommonPrefixes>\s*<Prefix>([^<]*)<\/Prefix>\s*<\/CommonPrefixes>/g),
  ].map((match) => unescapeXml(match[1]));
  if (!/<IsTruncated>true<\/IsTruncated>/.test(xml)) return { prefixes, next: null };
  const token = /<NextContinuationToken>([^<]+)<\/NextContinuationToken>/.exec(xml)?.[1];
  if (!token) throw Error("Truncated R2 listing without a continuation token");
  return { prefixes, next: unescapeXml(token) };
}

export function parseTag(tag) {
  const match = tagPattern.exec(tag);
  if (!match) return null;
  const [, kind, vendor, model, version] = match;
  const board = vendor + "/" + model;
  return releaseTag(kind, board, version) === tag ? { kind, board, version } : null;
}

// Match receipt files exactly as the Hub resolved them before this index existed.
export function mapRoles({ board, kind, version }, assets) {
  const files = {};
  const missing = [];
  for (const [asset, template] of Object.entries(
    assetNames(board, "{chip}", kind, version),
  )) {
    const [start, end] = template.split("{chip}");
    const file = assets.find(({ name }) =>
      typeof name === "string" &&
      (end === undefined
        ? name === start
        : name.startsWith(start) && name.endsWith(end) && !name.includes("/")),
    );
    if (file) files[asset] = file;
    else missing.push(asset);
  }
  return { files, missing };
}

// Report latest releases that lack a matching tag or any of their files.
export function findGaps(latest, indexed) {
  const gaps = [];
  for (const { board, kind, version, tag } of latest) {
    const expected = releaseTag(kind, board, version);
    if (tag !== expected) {
      gaps.push(`${board} ${kind} ${version}: tag ${tag ?? "missing"}, expected ${expected}`);
      continue;
    }
    const have = new Set(indexed.get(tag) ?? []);
    const missing = Object.keys(assetNames(board, "", kind, version)).filter(
      (asset) => !have.has(asset),
    );
    if (missing.length)
      gaps.push(`${board} ${kind} ${version}: missing ${missing.join(", ")}`);
  }
  return gaps;
}

export function s3Store(endpoint, aws) {
  return {
    async *prefixes(prefix) {
      let next = null;
      do {
        const url = new URL(bucket, endpoint);
        url.searchParams.set("list-type", "2");
        url.searchParams.set("prefix", prefix);
        url.searchParams.set("delimiter", "/");
        if (next) url.searchParams.set("continuation-token", next);
        const response = await aws.fetch(url);
        if (!response.ok) throw Error(`R2 listing failed (HTTP ${response.status})`);
        const page = parseListing(await response.text());
        yield* page.prefixes;
        next = page.next;
      } while (next);
    },
    async json(key) {
      const response = await aws.fetch(new URL(bucket + "/" + key, endpoint));
      if (response.status === 404) return null;
      if (!response.ok) throw Error(`R2 read failed (HTTP ${response.status}): ${key}`);
      return response.json();
    },
  };
}

async function mapLimit(items, limit, task) {
  const results = new Array(items.length);
  let index = 0;
  const worker = async () => {
    while (index < items.length) {
      const current = index++;
      results[current] = await task(items[current]);
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker));
  return results;
}

export async function indexReleases(sql, store, { concurrency = 16, log = console.log } = {}) {
  const prefixes = [];
  for await (const prefix of store.prefixes("releases/")) prefixes.push(prefix);
  const skipped = [];
  const unpublished = [];
  const incomplete = [];
  const releases = await mapLimit(prefixes, concurrency, async (prefix) => {
    const tag = prefix.slice("releases/".length, -1);
    const release = parseTag(tag);
    if (!release) return void skipped.push(tag);
    const receipt = await store.json(prefix + "published.json");
    if (!Array.isArray(receipt?.assets)) return void unpublished.push(tag);
    const { files, missing } = mapRoles(release, receipt.assets);
    if (missing.length) incomplete.push(`${tag} (${missing.join(", ")})`);
    return { ...release, tag, files };
  });
  const indexedReleases = releases.filter(Boolean);
  const rows = indexedReleases.flatMap(assetRows);
  await sql.begin(async (tx) => {
    await recordAssets(tx, rows);
    const latest = (
      await tx`SELECT board, kind, version FROM esphub.firmware_releases WHERE tag IS NULL`
    ).map((row) => ({ ...row, tag: releaseTag(row.kind, row.board, row.version) }));
    if (latest.length)
      await tx`UPDATE esphub.firmware_releases r SET tag = l.tag
        FROM jsonb_to_recordset(${tx.json(latest)}::jsonb)
          AS l(board text, kind text, version text, tag text)
        WHERE r.board = l.board AND r.kind = l.kind AND r.version = l.version AND r.tag IS NULL`;
  });
  log(
    `Indexed ${rows.length} files from ${indexedReleases.length} releases; ` +
      `skipped ${skipped.length} unrecognised and ${unpublished.length} unpublished prefixes`,
  );
  for (const tag of skipped) log("Skipped unrecognised prefix: " + tag);
  for (const tag of unpublished) log("Skipped prefix without a receipt: " + tag);
  for (const entry of incomplete) log("Receipt lacks files: " + entry);

  const latest = await sql`SELECT board, kind, version, tag FROM esphub.firmware_releases ORDER BY board, kind`;
  const indexed = new Map(
    (
      await sql`SELECT a.tag, array_agg(a.asset) AS assets FROM esphub.release_assets a
        JOIN esphub.firmware_releases r ON r.tag = a.tag GROUP BY a.tag`
    ).map(({ tag, assets }) => [tag, assets]),
  );
  return { files: rows.length, latest: latest.length, gaps: findGaps(latest, indexed) };
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  const { FIRMWARE_R2_ENDPOINT, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, FIRMWARE_DATABASE_URL } =
    process.env;
  if (!FIRMWARE_R2_ENDPOINT || !AWS_ACCESS_KEY_ID || !AWS_SECRET_ACCESS_KEY)
    throw Error("Missing firmware R2 credentials");
  if (!FIRMWARE_DATABASE_URL) throw Error("Missing FIRMWARE_DATABASE_URL");
  const aws = new AwsClient({
    accessKeyId: AWS_ACCESS_KEY_ID,
    secretAccessKey: AWS_SECRET_ACCESS_KEY,
    service: "s3",
    region: "auto",
  });
  const sql = postgres(FIRMWARE_DATABASE_URL, { max: 1, prepare: false });
  try {
    const { latest, gaps } = await indexReleases(sql, s3Store(FIRMWARE_R2_ENDPOINT, aws));
    if (gaps.length) {
      for (const gap of gaps) console.error("Not indexed: " + gap);
      throw Error(`${gaps.length} of ${latest} latest releases are not fully indexed`);
    }
    console.log(`All ${latest} latest releases are tagged and fully indexed`);
  } finally {
    await sql.end();
  }
}
