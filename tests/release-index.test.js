import assert from "node:assert/strict";
import { createServer } from "node:http";
import test from "node:test";
import { AwsClient } from "aws4fetch";
import postgres from "postgres";
import { assetNames, releaseTag } from "../shared/firmware-releases.js";
import {
  findGaps,
  indexReleases,
  mapRoles,
  parseListing,
  parseTag,
  s3Store,
} from "../scripts/index-releases.mjs";
import { advanceLatest, assetRows, recordAssets } from "../scripts/release-records.mjs";

const receipt = (board, chip, kind, version) => ({
  assets: Object.values(assetNames(board, chip, kind, version)).map((name, i) => ({
    name,
    size: 100 + i,
    sha256: "0".repeat(64),
  })),
});

test("parses delimited R2 listing pages", () => {
  const page = parseListing(`<?xml version="1.0"?><ListBucketResult>
    <IsTruncated>true</IsTruncated>
    <CommonPrefixes><Prefix>releases/a/</Prefix></CommonPrefixes>
    <CommonPrefixes><Prefix>releases/b&amp;c/</Prefix></CommonPrefixes>
    <NextContinuationToken>tok&amp;en</NextContinuationToken></ListBucketResult>`);
  assert.deepEqual(page, { prefixes: ["releases/a/", "releases/b&c/"], next: "tok&en" });
  assert.deepEqual(
    parseListing("<ListBucketResult><IsTruncated>false</IsTruncated></ListBucketResult>"),
    { prefixes: [], next: null },
  );
  assert.throws(() =>
    parseListing("<ListBucketResult><IsTruncated>true</IsTruncated></ListBucketResult>"),
  );
});

test("accepts only current release tags that round-trip", () => {
  for (const kind of ["bootloader", "app", "setup"])
    assert.deepEqual(parseTag(releaseTag(kind, "m5stack/basic-v27", "2.0.30")), {
      kind,
      board: "m5stack/basic-v27",
      version: "2.0.30",
    });
  for (const tag of [
    "firmware-micropython-m5stack_atoms3-v1.0.0",
    "firmware-app-stopwatch-v2.0.1",
    "firmware-app-m5stack_stop_watch-v2.0.1",
    "firmware-app-M5stack_stopwatch-v2.0.1",
    "firmware-app-m5stack_stopwatch-v2.0",
    "firmware-app-m5stack_stopwatch-v2.0.1/x",
  ])
    assert.equal(parseTag(tag), null, tag);
});

test("maps receipt files to the assets the Hub served", () => {
  for (const kind of ["bootloader", "app", "setup"]) {
    const release = { board: "m5stack/stopwatch", kind, version: "2.0.41" };
    const { assets } = receipt(release.board, "esp32s3", kind, release.version);
    const { files, missing } = mapRoles(release, assets);
    assert.deepEqual(missing, []);
    assert.deepEqual(
      Object.fromEntries(Object.entries(files).map(([asset, { name }]) => [asset, name])),
      assetNames(release.board, "esp32s3", kind, release.version),
    );
  }
  const release = { board: "m5stack/stopwatch", kind: "bootloader", version: "2.0.41" };
  const { files, missing } = mapRoles(release, [
    { name: "nested/esphub-m5stack_stopwatch-esp32s3-2.0.41-ota.bin" },
    { name: "esphub-m5stack_stopwatch-esp32s3-2.0.41.json", size: 7 },
  ]);
  assert.deepEqual(Object.keys(files), ["manifest"]);
  assert.deepEqual(missing, ["ota", "usb"]);
});

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

test("reports latest releases that are untagged or partly indexed", () => {
  const app = releaseTag("app", "m5stack/core2", "2.0.5");
  const boot = releaseTag("bootloader", "m5stack/core2", "2.0.5");
  const gaps = findGaps(
    [
      { board: "m5stack/core2", kind: "app", version: "2.0.5", tag: app },
      { board: "m5stack/core2", kind: "bootloader", version: "2.0.5", tag: boot },
      { board: "m5stack/core2", kind: "setup", version: "2.0.5", tag: null },
    ],
    new Map([[app, ["app"]], [boot, ["manifest", "ota"]]]),
  );
  assert.equal(gaps.length, 2);
  assert.match(gaps[0], /bootloader 2\.0\.5: missing usb/);
  assert.match(gaps[1], /setup 2\.0\.5: tag missing/);
});

test("lists every page of R2 release prefixes and reads receipts", async (t) => {
  const requests = [];
  const server = createServer((req, res) => {
    const url = new URL(req.url, "http://localhost");
    requests.push(url.pathname + url.search);
    assert.match(req.headers.authorization, /^AWS4-HMAC-SHA256 /);
    if (url.pathname === "/esphub-firmware" && url.searchParams.get("list-type") === "2") {
      assert.equal(url.searchParams.get("prefix"), "releases/");
      assert.equal(url.searchParams.get("delimiter"), "/");
      const second = url.searchParams.get("continuation-token") === "page-2";
      res.end(`<ListBucketResult><IsTruncated>${!second}</IsTruncated>
        <CommonPrefixes><Prefix>releases/${second ? "b" : "a"}/</Prefix></CommonPrefixes>
        ${second ? "" : "<NextContinuationToken>page-2</NextContinuationToken>"}</ListBucketResult>`);
    } else if (url.pathname === "/esphub-firmware/releases/a/published.json") {
      res.setHeader("Content-Type", "application/json");
      res.end('{"assets":[]}');
    } else {
      res.statusCode = 404;
      res.end();
    }
  });
  await new Promise((done) => server.listen(0, "127.0.0.1", done));
  t.after(() => server.close());
  const store = s3Store(
    `http://127.0.0.1:${server.address().port}`,
    new AwsClient({ accessKeyId: "id", secretAccessKey: "secret", service: "s3", region: "auto" }),
  );
  const prefixes = [];
  for await (const prefix of store.prefixes("releases/")) prefixes.push(prefix);
  assert.deepEqual(prefixes, ["releases/a/", "releases/b/"]);
  assert.deepEqual(await store.json("releases/a/published.json"), { assets: [] });
  assert.equal(await store.json("releases/b/published.json"), null);
  assert.equal(requests.filter((path) => path.includes("list-type=2")).length, 2);
});

// Requires an EspHub database migrated through esphub migration 020. Changes are rolled back.
test(
  "indexes published releases and tags the latest rows",
  { skip: !process.env.TEST_DATABASE_URL },
  async () => {
    const sql = postgres(process.env.TEST_DATABASE_URL, { max: 1, prepare: false });
    const rollback = Symbol("rollback");
    try {
      await sql.begin(async (tx) => {
        const board = "index-test/" + crypto.randomUUID().slice(0, 8);
        const tags = Object.fromEntries(
          ["2.0.1", "2.0.2"].flatMap((version) =>
            ["bootloader", "app", "setup"].map((kind) => [
              `${kind}@${version}`,
              releaseTag(kind, board, version),
            ]),
          ),
        );
        const receipts = new Map();
        for (const [key, tag] of Object.entries(tags)) {
          const [kind, version] = key.split("@");
          receipts.set(`releases/${tag}/published.json`, receipt(board, "esp32s3", kind, version));
        }
        receipts.delete(`releases/${tags["setup@2.0.1"]}/published.json`);
        const store = {
          async *prefixes() {
            yield* Object.values(tags).map((tag) => `releases/${tag}/`);
            yield "releases/firmware-micropython-m5stack_atoms3-v1.0.0/";
          },
          async json(key) {
            return receipts.get(key) ?? null;
          },
        };
        // The pre-index publisher wrote versions without tags.
        await tx`INSERT INTO esphub.firmware_releases (board, kind, version)
          VALUES (${board}, 'bootloader', '2.0.2'), (${board}, 'app', '2.0.2'), (${board}, 'setup', '2.0.2')`;
        const logs = [];
        const tagsOnly = async (db) =>
          db`SELECT kind, tag FROM esphub.firmware_releases WHERE board = ${board} ORDER BY kind`;
        const run = async () => {
          // indexReleases opens its own transaction; run it on this rolled-back one.
          const scoped = Object.assign((...args) => tx(...args), tx, {
            begin: (body) => body(tx),
          });
          return indexReleases(scoped, store, { log: (line) => logs.push(line) });
        };
        const first = await run();
        assert.deepEqual(first.gaps.filter((gap) => gap.startsWith(board)), []);
        assert.match(logs[0], /from 5 releases; skipped 1 unrecognised and 1 unpublished/);
        assert.deepEqual(
          (await tagsOnly(tx)).map(({ kind, tag }) => [kind, tag]),
          [["app", tags["app@2.0.2"]], ["bootloader", tags["bootloader@2.0.2"]], ["setup", tags["setup@2.0.2"]]],
        );
        const [ota] = await tx`SELECT object_key, size FROM esphub.release_assets
          WHERE tag = ${tags["bootloader@2.0.1"]} AND asset = 'ota'`;
        assert.deepEqual(ota, {
          object_key: `releases/${tags["bootloader@2.0.1"]}/` +
            assetNames(board, "esp32s3", "bootloader", "2.0.1").ota,
          size: "101",
        });
        const [{ count }] = await tx`SELECT count(*)::int AS count FROM esphub.release_assets WHERE board = ${board}`;
        assert.equal(count, 2 * 3 + 2 * 1 + 2); // bootloader and app twice, setup only for 2.0.2
        const second = await run();
        assert.equal(second.files, first.files);
        const [{ count: again }] = await tx`SELECT count(*)::int AS count FROM esphub.release_assets WHERE board = ${board}`;
        assert.equal(again, count);

        // The publisher advances only to newer versions and moves the tag with the version.
        const app3 = { board, kind: "app", version: "2.0.3", tag: releaseTag("app", board, "2.0.3") };
        await recordAssets(tx, assetRows({ ...app3, files: { app: { name: "x.bin", size: 9 } } }));
        await advanceLatest(tx, app3);
        await advanceLatest(tx, { board, kind: "app", version: "2.0.2", tag: tags["app@2.0.2"] });
        const [app] = await tx`SELECT version, tag FROM esphub.firmware_releases WHERE board = ${board} AND kind = 'app'`;
        assert.deepEqual(app, { version: "2.0.3", tag: app3.tag });
        throw rollback;
      });
    } catch (error) {
      if (error !== rollback) throw error;
    } finally {
      await sql.end();
    }
  },
);
