import { boardFileStem } from "./board-id.js";

export function releaseTag(kind, board, version) {
  return `firmware-${kind}-${boardFileStem(board)}-v${version}`;
}

export function releaseVersion(tag, kind, board) {
  const prefix = releaseTag(kind, board, "");
  const version = tag.startsWith(prefix) ? tag.slice(prefix.length) : "";
  return /^\d+\.\d+\.\d+$/.test(version) ? version : null;
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
