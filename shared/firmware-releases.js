import { boardFileStem } from "./board-id.js";

export function releaseTag(kind, board, version) {
  return `firmware-${kind}-${boardFileStem(board)}-v${version}`;
}

// Published files per release. A bootloader release publishes only its OTA image;
// its USB base image and build manifest are inputs to the setup kit.
export function assetNames(board, chip, kind, version) {
  const stem = boardFileStem(board);
  const base = `esphub-${stem}-${chip}-${version}`;
  if (kind === "bootloader") return { ota: base + "-ota.bin" };
  if (kind === "setup")
    return {
      setup: base + "-setup.zip",
      "setup-manifest": base + "-setup.json",
    };
  return { [kind]: `esphub-${stem}-${kind}-${version}.bin` };
}
