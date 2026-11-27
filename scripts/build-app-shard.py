#!/usr/bin/env python3
"""Compile the selected boards in one shard using a shared compiler cache."""

import argparse
import json
import os
from pathlib import Path
import runpy
import shutil
from tempfile import TemporaryDirectory

build = runpy.run_path(str(Path(__file__).with_name("build-example.py")))["build"]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--version", required=True)
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("boards", nargs="+")
args = parser.parse_args()

os.environ["CMAKE_C_COMPILER_LAUNCHER"] = "sccache"
os.environ["CMAKE_CXX_COMPILER_LAUNCHER"] = "sccache"
with TemporaryDirectory(prefix="esphub-app-shard-") as directory:
    root = Path(directory)
    os.environ["SCCACHE_BASEDIRS"] = str(root)
    work = root / "work"
    for board_id in args.boards:
        print(f"::group::{board_id}", flush=True)
        board = json.loads((Path("firmware/board") / board_id / "board.json").read_text())
        options = argparse.Namespace(
            board=board_id, version=args.version, app_size_bytes=None,
            out=args.out / board_id.replace("/", "_"),
        )
        for example in board["examples"]:
            work.mkdir()
            try:
                build(options, board, example, work)
            finally:
                shutil.rmtree(work)
        print("::endgroup::", flush=True)
