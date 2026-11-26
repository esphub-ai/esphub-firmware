#!/usr/bin/env python3
"""Measure ESP32-S3 compiler cache reuse using the normal firmware build function."""

from argparse import Namespace
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
from tempfile import TemporaryDirectory
from time import monotonic

build = runpy.run_path("scripts/build-example.py")["build"]
board_a = "m5stack/atoms3"
board_b = "m5stack/atoms3u"
board_c = "waveshare/esp32-s3-zero-n8r8"
launcher_settings = ("CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER")


def sccache(*arguments):
    return subprocess.check_output(["sccache", *arguments], text=True)


def measure(label, board_id, version, use_cache, work, out):
    for key in launcher_settings:
        if use_cache:
            os.environ[key] = "sccache"
        else:
            os.environ.pop(key, None)
    sccache("--zero-stats")
    board = json.loads(
        Path("firmware/board", board_id, "board.json").read_text()
    )
    if board["chip"] != "esp32s3":
        raise ValueError(f"Unexpected chip for {board_id}")
    example = next(item for item in board["examples"] if item["id"] == board["default_example"])
    args = Namespace(board=board_id, version=version, app_size_bytes=None, out=out)
    work.mkdir()
    try:
        start = monotonic()
        build(args, board, example, work)
        elapsed = monotonic() - start
    finally:
        shutil.rmtree(work)
    stats = json.loads(sccache("--show-stats", "--stats-format=json"))["stats"]
    hits = sum(stats["cache_hits"]["counts"].values())
    misses = sum(stats["cache_misses"]["counts"].values())
    if use_cache and not hits + misses:
        raise RuntimeError("ESP-IDF did not issue any cacheable compiler calls")
    row = f"| {label} | {board_id} | {elapsed:.1f} | {hits} | {misses} |"
    print(row, flush=True)
    print(sccache("--show-stats"), flush=True)
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
        summary.write(row + "\n")


with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
    summary.write(
        f"Restored cache: {os.getenv('RESTORED_CACHE') or 'none'}\n\n"
        "| Configuration | Board | Seconds | Hits | Misses |\n"
        "| --- | --- | ---: | ---: | ---: |\n"
    )

with TemporaryDirectory(prefix="esphub-benchmark-") as temp:
    root = Path(temp)
    work = root / "work"
    os.environ["SCCACHE_BASEDIRS"] = str(root)
    sccache("--start-server")
    measure("No compiler cache", board_a, "2.0.100", False, work, root / "baseline")
    measure("First cached build", board_a, "2.0.100", True, work, root / "first")
    measure("Same board, new version", board_a, "2.0.101", True, work, root / "warm")
    measure("Second board, same chip", board_b, "2.0.101", True, work, root / "second")
    measure("Third board, same chip", board_c, "2.0.101", True, work, root / "third")
