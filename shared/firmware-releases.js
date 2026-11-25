import { boardFileStem } from "./board-id.js";

export function releaseTag(kind, board, version) {
  return `firmware-${kind}-${boardFileStem(board)}-v${version}`;
}

export function releaseVersion(tag, kind, board) {
  const prefix = releaseTag(kind, board, "");
  const version = tag.startsWith(prefix) ? tag.slice(prefix.length) : "";
  return /^\d+\.\d+\.\d+$/.test(version) ? version : null;
}

export function newer(a, b) {
  const left = a.split(".").map(Number),
    right = b.split(".").map(Number);
  return (left[0] - right[0] || left[1] - right[1] || left[2] - right[2]) > 0;
}

export function assetNames(board, chip, kind, version) {
  const stem = boardFileStem(board);
  const base = `esphub-${stem}-${chip}-${version}`;
  if (kind === "bootloader")
    return {
      manifest: base + ".json",
      ota: base + "-ota.bin",
      usb: base + "-usb.zip",
    };
  if (kind === "setup")
    return {
      setup: base + "-setup.zip",
      "setup-manifest": base + "-setup.json",
    };
  return { [kind]: `esphub-${stem}-${kind}-${version}.bin` };
}

export async function publishedFirmware(
  boards,
  request = fetch,
  token,
  repository = "okou-ai/esphub",
) {
  const index = Object.fromEntries(Object.keys(boards).map((id) => [id, {}]));
  const headers = {
    "User-Agent": "ESPHub-firmware",
    Accept: "application/vnd.github+json",
  };
  if (token) headers.Authorization = "Bearer " + token;
  for (let page = 1; ; page++) {
    const url = `https://api.github.com/repos/${repository}/releases?per_page=100&page=${page}`;
    const res = await request(url, {
      headers,
      cf: { cacheEverything: true, cacheTtl: 120 },
    });
    if (!res.ok) throw Error("GitHub Releases unavailable: " + res.status);
    const releases = await res.json();
    for (const release of releases) {
      if (release.draft || release.prerelease) continue;
      const tag = release.tag_name,
        [namespace, kind] = tag.split("-");
      if (namespace !== "firmware") continue;
      if (!["bootloader", "micropython", "native", "setup"].includes(kind))
        continue;
      const assets = new Set(
        release.assets.filter((a) => a.state === "uploaded").map((a) => a.name),
      );
      for (const [board, { chip, runtime }] of Object.entries(boards)) {
        if (
          runtime &&
          ["micropython", "native"].includes(kind) &&
          kind !== runtime
        )
          continue;
        const version = releaseVersion(tag, kind, board),
          current = index[board][kind];
        if (!version || (current && !newer(version, current.version))) continue;
        if (
          Object.values(assetNames(board, chip, kind, version)).every((name) =>
            assets.has(name),
          )
        )
          index[board][kind] = {
            tag,
            version,
            fingerprint:
              release.body?.match(/^Build inputs: ([0-9a-f]{64})$/m)?.[1] ??
              null,
          };
      }
    }
    if (!res.headers.get("Link")?.includes('rel="next"')) return index;
  }
}
