import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

// Compile the Launcher's generator with a host CRC routine. The reference digest
// hashes the first 128 bytes of blocks 0 and 1 that upstream lfs_format (disk 2.1,
// block_size=4096, prog_size=128, name_max=63) writes, in capacity order.
test("Launcher empty LittleFS roots match lfs_format for every 64 KiB capacity", () => {
  const dir = mkdtempSync(join(tmpdir(), "esphub-littlefs-"));
  try {
    const exe = join(dir, "roots");
    execFileSync("cc", ["-std=c11", "-Wall", "-Wextra", "-Werror",
      "-Itests/fixtures/littlefs-root", "-Ifirmware/bootloader/common/main",
      "tests/fixtures/littlefs-root/roots.c", "-o", exe]);
    const roots = execFileSync(exe, { maxBuffer: 1 << 20 });
    assert.equal(roots.length, 470 * 256);
    assert.equal(createHash("sha256").update(roots).digest("hex"),
      "13d9fda50d2472106418bc360ef6e493bc7aec99409d96770594ce3e23e7cceb");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
