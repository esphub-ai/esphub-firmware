"""Run one USB kit's original Python example on the MicroPython Unix port."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from zipfile import ZipFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("manifest", type=Path)
parser.add_argument("--micropython", type=Path, required=True)
args = parser.parse_args()
meta = json.loads(args.manifest.read_text())
scripts = Path(__file__).resolve().parent
kit = args.manifest.parent / meta["file"]
board = meta["board"]
assert hashlib.sha256(kit.read_bytes()).hexdigest() == meta["sha256"]
with ZipFile(kit) as z, tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    rows = csv.reader(
        z.read("partitions.csv").decode().splitlines(), skipinitialspace=True
    )
    size = next(int(row[4], 0) for row in rows if row and row[0] == "data")
    (root / "main.py").write_bytes(z.read("main.py"))
    (root / "case.txt").write_text(board)
    (root / "model.txt").write_text(("simulated " + board) + "\n" + str(size))
    for name in ("input-model.py", "panel-model.py"):
        shutil.copy(scripts / name, root)
    subprocess.run(
        [args.micropython.resolve(), scripts / "host.py"], cwd=root, check=True
    )
