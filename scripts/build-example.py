#!/usr/bin/env python3
"""Build an EspHub board example with ESP-IDF and emit its OTA app image."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"


def checkout(parent, source):
    directory = parent / source["repository"].split("/")[-1]
    subprocess.run(["git", "init", "-q", directory], check=True)
    subprocess.run(["git", "-C", directory, "fetch", "-q", "--depth=1",
                    "https://github.com/" + source["repository"] + ".git", source["ref"]], check=True)
    subprocess.run(["git", "-C", directory, "checkout", "-q", "--detach", "FETCH_HEAD"], check=True)
    return str(directory)


def build(args, board, example, work):
    directory = FIRMWARE / "board" / args.board
    example_id = example["id"]
    entrypoint = directory / example["entrypoint"]
    partitions = FIRMWARE / "partitions" / f"{board['flash_mb']}mb.csv"
    env = dict(os.environ, IDF_TARGET=board["chip"])
    config = work / "sdkconfig"
    options = [f"-DSDKCONFIG={config}", f"-DPROJECT_VER={args.version}"]
    if args.board == "espressif/esp-mosaico":
        original = entrypoint.parents[1]
        project = work / "project"
        shutil.copytree(original, project)
        options.append(f"-DEH_AGENT_DIR={FIRMWARE / 'components/eh_agent'}")
        with (project / "sdkconfig.defaults").open("a") as defaults:
            defaults.write(f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{partitions}"\n')
        upstream = json.loads((original.parent / "upstream.json").read_text())
        for component, source in upstream.items():
            env["MOSAICO_" + component.upper()] = checkout(work, source)
    else:
        # ESP-IDF writes dependencies.lock and managed_components in its project
        # directory. Give each build its own project, including concurrent builds.
        project = work / "project"
        shutil.copytree(FIRMWARE / "app", project)
        sdkconfig = project / "sdkconfig.defaults"
        with sdkconfig.open("a") as defaults:
            example_defaults = entrypoint.parent / "sdkconfig.defaults"
            if example_defaults.is_file():
                defaults.write("\n" + example_defaults.read_text() + "\n")
            defaults.write(f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{partitions}"\n')
            defaults.write(f"CONFIG_ESPTOOLPY_FLASHSIZE_{board['flash_mb']}MB=y\n")
            if board.get("psram"):
                defaults.write("CONFIG_SPIRAM=y\nCONFIG_SPIRAM_USE_MALLOC=y\n")
                if board["psram"] == "octal":
                    defaults.write("CONFIG_SPIRAM_MODE_OCT=y\n")
        # The copy of app/ is independent of its original relative path to the SDK.
        options += [f"-DEH_AGENT_DIR={FIRMWARE / 'components/eh_agent'}",
                    f"-DEH_EXAMPLE_DIR={entrypoint.parent}", f"-DEH_APP_NAME={example_id}"]
        dependencies = entrypoint.parent / "idf_component.yml"
        if "m5_board_id" in board:
            options.append(f"-DEH_M5_BOARD_ID={board['m5_board_id']}")
            if not dependencies.exists():
                dependencies = project / "m5.yml"
        if dependencies.exists():
            shutil.copy(dependencies, project / "main/idf_component.yml")
    output = work / "build"
    subprocess.run(["idf.py", "-C", project, "-B", output, *options, "app"], env=env, check=True)
    description = json.loads((output / "project_description.json").read_text())
    image = (output / description["app_bin"]).read_bytes()
    with partitions.open() as source:
        app = next(row for row in csv.reader(source, skipinitialspace=True) if row and row[0] == "app")
    if not image or image[0] != 0xE9 or len(image) > int(app[4], 0):
        raise SystemExit("Invalid or oversized ESP application image")
    args.out.mkdir(parents=True, exist_ok=True)
    name = f"esphub-{args.board.replace('/', '_')}-app-{args.version}"
    if example_id != board["default_example"]:
        name += "-" + example_id
    (args.out / (name + ".bin")).write_bytes(image)
    (args.out / (name + ".sha256")).write_text(f"{hashlib.sha256(image).hexdigest()}  {name}.bin\n")
    print(args.out / (name + ".bin"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--example", help="Example ID (defaults to the board’s factory example)")
    selection.add_argument("--all", action="store_true", help="Build every example for the board")
    parser.add_argument("--version", default="0.0.0-dev")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args()
    board = json.loads((FIRMWARE / "board" / args.board / "board.json").read_text())
    selected = args.example or board["default_example"]
    examples = [e for e in board["examples"] if args.all or e["id"] == selected]
    if not examples:
        parser.error("Unknown example for this board: " + selected)
    for example in examples:
        with TemporaryDirectory(prefix="eh-example-") as temp:
            build(args, board, example, Path(temp))
