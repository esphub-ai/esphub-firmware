import { boardFileStem } from "./board-id.js";

export function releaseTag(kind, board, version) {
  return `firmware-${kind}-${boardFileStem(board)}-v${version}`;
}

// A board release publishes only its default app. Bootloader sets are published by ID.
export function appAssetName(board, version) {
  return `esphub-${boardFileStem(board)}-app-${version}.bin`;
}
