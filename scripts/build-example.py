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
import sys
import tarfile
from urllib.request import urlopen
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"


def checkout(parent, source):
    directory = parent / source["repository"].split("/")[-1]
    subprocess.run(["git", "init", "-q", directory], check=True)
    subprocess.run(["git", "-C", directory, "fetch", "-q", "--depth=1",
                    "https://github.com/" + source["repository"] + ".git", source["ref"]], check=True)
    subprocess.run(["git", "-C", directory, "checkout", "-q", "--detach", "FETCH_HEAD"], check=True)
    return str(directory)


def build_partitions(flash_mb, app_size, work):
    source = FIRMWARE / "partitions" / f"{flash_mb}mb.csv"
    if app_size is None:
        return source
    with source.open() as table:
        rows = list(csv.reader((line for line in table if line.strip() and not line.startswith("#")),
                               skipinitialspace=True))
    app, data = rows[-2:]
    if app[0] != "app" or data[0] != "data":
        raise ValueError("Expected app and data as the last two partitions")
    data_start = int(app[3], 0) + app_size
    data_size = int(data[3], 0) + int(data[4], 0) - data_start
    if app_size <= 0 or app_size % 65536 or data_size < 8192:
        raise ValueError("App size must be a positive multiple of 65536 and leave space for LittleFS")
    app[4], data[3], data[4] = hex(app_size), hex(data_start), hex(data_size)
    output = work / "partitions.csv"
    with output.open("w", newline="") as table:
        csv.writer(table, lineterminator="\n").writerows(rows)
    return output



def write_mosaico_notices(output, project, upstream_root, description):
    sources = list(dict.fromkeys([upstream_root, Path(description["idf_path"])] +
                                 [Path(path) for path in description["build_component_paths"]]))
    with ZipFile(output, "w", ZIP_DEFLATED) as notices:
        for index, directory in enumerate(sources):
            for path in sorted(directory.iterdir()):
                name = path.name.upper()
                if path.is_file() and (name.split(".", 1)[0] in
                                       {"LICENSE", "LICENCE", "COPYING", "NOTICE",
                                        "THIRD_PARTY_NOTICES", "ASSET_PROVENANCE"}
                                       or name.startswith(("LICENSE-", "COPYING-", "NOTICE-"))):
                    notices.write(path, f"{index:03d}-{directory.name}/{path.name}")
        lock = project / "dependencies.lock"
        if lock.is_file():
            notices.write(lock, "dependencies.lock")


def build(args, board, example, work):
    directory = FIRMWARE / "board" / args.board
    example_id = example["id"]
    entrypoint = directory / example["entrypoint"]
    partitions = build_partitions(board["flash_mb"], args.app_size_bytes, work)
    env = dict(os.environ, IDF_TARGET=board["chip"])
    config = work / "sdkconfig"
    options = [f"-DSDKCONFIG={config}", f"-DPROJECT_VER={args.version}"]
    upstream_root = None
    raylib_example = args.board == "espressif/esp-mosaico" and entrypoint.name == "registry.json"
    if args.board == "espressif/esp-mosaico" and (raylib_example or entrypoint.suffix == ".patch"):
        manifest = entrypoint if raylib_example else entrypoint.parent / "upstream.json"
        upstream = json.loads(manifest.read_text())
        env["EH_AGENT_DIR"] = str(FIRMWARE / "components/eh_agent")
        if raylib_example:
            archive = work / "example.tgz"
            with urlopen(upstream["archive"], timeout=60) as download, archive.open("wb") as target:
                shutil.copyfileobj(download, target)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != upstream["sha256"]:
                raise SystemExit("Raylib example archive checksum mismatch")
            project = work / "project"
            upstream_root = project
            project.mkdir()
            with tarfile.open(archive, "r:gz") as bundle:
                bundle.extractall(project, filter="data")
            patch = entrypoint.parents[1] / "raylib-esphub.patch"
            subprocess.run(["git", "apply", patch], cwd=project, check=True)
            subprocess.run([sys.executable, "-m", "pip", "install", "esp-bmgr-assist==0.8.4",
                            "Pillow==12.0.0", "numpy==2.3.5"], check=True)
            subprocess.run(["idf.py", "bmgr", "-c", "shared/boards/esp-mosaico/bmgr",
                            "-b", "esp_mosaico"], cwd=project, env=env, check=True)
        else:
            source = Path(checkout(work, upstream))
            upstream_root = source
            subprocess.run(["git", "-C", source, "apply", entrypoint], check=True)
            project = source / upstream["project"]
        with (project / "sdkconfig.defaults").open("a") as defaults:
            defaults.write(f"\nCONFIG_ESPTOOLPY_FLASHSIZE_{board['flash_mb']}MB=y\n")
            defaults.write("CONFIG_PARTITION_TABLE_CUSTOM=y\n")
            defaults.write(f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{partitions}"\n')
            defaults.write("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y\n")
            defaults.write("CONFIG_MBEDTLS_PEM_WRITE_C=y\n")
            defaults.write("CONFIG_MBEDTLS_X509_CREATE_C=y\n")
            defaults.write("CONFIG_MBEDTLS_X509_CSR_WRITE_C=y\n")
            defaults.write("CONFIG_ESP_MAIN_TASK_STACK_SIZE=20480\n")
    elif args.board == "espressif/esp-mosaico":
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
            # OTA apps must use the flash bus selected for the board's bootloader.
            if board["bootloader"].endswith("-opi"):
                defaults.write("CONFIG_ESPTOOLPY_OCT_FLASH=y\n")
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
    if upstream_root is not None:
        write_mosaico_notices(args.out / (name + ".notices.zip"), project, upstream_root, description)
    print(args.out / (name + ".bin"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--example", help="Example ID (defaults to the board’s factory example)")
    selection.add_argument("--all", action="store_true", help="Build every example for the board")
    parser.add_argument("--version", default="0.0.0-dev")
    parser.add_argument("--app-size-bytes", type=lambda value: int(value, 0),
                        help="Target app slot on a resized device, in bytes (64 KiB aligned)")
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
