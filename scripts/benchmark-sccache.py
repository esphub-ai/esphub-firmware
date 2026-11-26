#!/usr/bin/env python3
"""Build every application example assigned to a fixed compiler-cache shard."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
from tempfile import TemporaryDirectory
from time import monotonic

BOARD_ROOT = Path("firmware/board")
SHARD_COUNT = 10


def build_plan(shard):
    profiles = json.loads(subprocess.check_output([
        "node", "--input-type=module", "-e",
        "import {profiles} from './scripts/firmware-plan.mjs';"
        "console.log(JSON.stringify(profiles('HEAD')));",
    ], text=True))
    groups = {}
    for board_id, profile in sorted(profiles.items()):
        board_hash = int(hashlib.sha256(board_id.encode()).hexdigest(), 16)
        if board_hash % SHARD_COUNT == shard:
            image = profile["tasks"]["app"]["container"]
            groups.setdefault(image, []).append(board_id)
    return groups


def sccache(*arguments):
    return subprocess.check_output(["sccache", *arguments], text=True)


def build_boards(boards, version, results):
    build = runpy.run_path("scripts/build-example.py")["build"]
    os.environ["CMAKE_C_COMPILER_LAUNCHER"] = "sccache"
    os.environ["CMAKE_CXX_COMPILER_LAUNCHER"] = "sccache"

    with TemporaryDirectory(prefix="esphub-benchmark-") as directory:
        root = Path(directory)
        work = root / "work"
        out = root / "out"
        os.environ["SCCACHE_BASEDIRS"] = str(root)
        sccache("--start-server")
        try:
            for board_id in boards:
                print(f"::group::{board_id}", flush=True)
                board = json.loads((BOARD_ROOT / board_id / "board.json").read_text())
                args = argparse.Namespace(board=board_id, version=version, app_size_bytes=None, out=out)
                sccache("--zero-stats")
                failed = []
                start = monotonic()
                for example in board["examples"]:
                    work.mkdir()
                    try:
                        build(args, board, example, work)
                    except (subprocess.CalledProcessError, OSError, ValueError, SystemExit) as error:
                        failed.append(example["id"])
                        print(f"{board_id}/{example['id']}: {error}", flush=True)
                    finally:
                        shutil.rmtree(work)
                elapsed = monotonic() - start
                stats = json.loads(sccache("--show-stats", "--stats-format=json"))["stats"]
                hits = sum(stats["cache_hits"]["counts"].values())
                misses = sum(stats["cache_misses"]["counts"].values())
                row = {
                    "board": board_id,
                    "examples": len(board["examples"]),
                    "failed": failed,
                    "seconds": round(elapsed, 2),
                    "hits": hits,
                    "misses": misses,
                }
                print("::endgroup::", flush=True)
                print(json.dumps(row), flush=True)
                with results.open("a") as output:
                    output.write(json.dumps(row) + "\n")
                shutil.rmtree(out, ignore_errors=True)
        finally:
            sccache("--stop-server")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan")
    plan.add_argument("shard", type=int, choices=range(SHARD_COUNT))
    build = commands.add_parser("build")
    build.add_argument("--version", required=True)
    build.add_argument("--results", type=Path, required=True)
    build.add_argument("boards", nargs="+")
    args = parser.parse_args()
    if args.command == "plan":
        print(json.dumps(build_plan(args.shard)))
    else:
        build_boards(args.boards, args.version, args.results)
