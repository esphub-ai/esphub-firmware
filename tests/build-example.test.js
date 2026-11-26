import test from "node:test";
import { execFileSync } from "node:child_process";

test("a custom example build uses the requested app slot without moving the Launchers", () => {
  execFileSync("python3", ["-c", `
import csv, runpy, tempfile
from pathlib import Path
partitions = runpy.run_path('scripts/build-example.py')['build_partitions']
def rows(path):
    with path.open() as table:
        return list(csv.reader((line for line in table if line.strip() and not line.startswith('#')), skipinitialspace=True))
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    base = rows(partitions(8, None, work))
    for size in [3 << 20, 5 << 20, 0x550000]:
        custom = rows(partitions(8, size, work))
        assert custom[:-2] == base[:-2] and custom[-2][:4] == base[-2][:4]
        start = int(custom[-2][3], 0)
        assert int(custom[-2][4], 0) == size
        assert int(custom[-1][3], 0) == start + size
        assert int(custom[-1][4], 0) == (8 << 20) - start - size
    for size in [0, 0x500001, 0x560000]:
        try: partitions(8, size, work)
        except ValueError: continue
        raise AssertionError(size)
`]);
});
