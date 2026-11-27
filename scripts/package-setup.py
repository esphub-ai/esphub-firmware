"""Complete a fresh-device USB kit using the board's bootloader build and application image."""
import argparse
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("board")
parser.add_argument("bootloader", type=Path)
parser.add_argument("app", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
group, project = args.board.split("/")
root = Path(__file__).resolve().parents[1]
profile = root / "firmware/board" / group / project
recipe = {"tag": os.environ["TAG"], "bootloader_version": os.environ["BOOTLOADER_VERSION"],
          "app_version": os.environ["APP_VERSION"], "bootloader_tag": os.environ["BOOTLOADER_TAG"],
          "app_tag": os.environ["APP_TAG"]}
metadata = json.loads((profile / "board.json").read_text())
chip = metadata["chip"]
example = json.loads(subprocess.check_output(
    ["node", "scripts/board-catalog.mjs", "--default-example", args.board], cwd=root, text=True))
version = os.environ["VERSION"]
prefix = f"esphub-{group}_{project}-{chip}-{version}-setup"
kit = args.output / (prefix + ".zip")
image = args.app.read_bytes()
with ZipFile(args.bootloader) as base, ZipFile(kit, "w", ZIP_DEFLATED) as dest:
    rows = csv.reader(base.read("partitions.csv").decode().splitlines(), skipinitialspace=True)
    parts = {row[0]: row for row in rows if row and not row[0].startswith("#")}
    app = parts["app"]
    offset, size = int(app[3], 0), int(app[4], 0)
    assert parts["data"][2] == "littlefs" and 0 < len(image) <= size
    bootloader = base.read("esp-bootloader.bin")
    variant = metadata["bootloader"]
    assert bootloader[0] == 0xE9 and bootloader[288:].startswith(
        f"ESPHUB2/bootloader/{variant}\0bootloader-ota-v2\0".encode())
    assert image[0] == 0xE9
    assert bootloader[12:14] == image[12:14], "Mismatched ESP chips"
    for name in base.namelist():
        if name not in ("flash_args", "INSTALL.txt"):
            dest.writestr(name, base.read(name))
    app_file = "app.bin"
    dest.writestr(app_file, image)
    dest.writestr("flash_args", base.read("flash_args").rstrip() + f"\n{offset:#x} {app_file}\n".encode())
    nvs = {"offset": parts["nvs"][3], "size": int(parts["nvs"][4], 0)}
    assert parts["nvs"][1:3] == ["data", "nvs"]
    console = "the USB console"
    note = ""
    if args.board == "espressif/esp-mosaico":
        console = "the UART console on header H1"
        note = ("Flash through onboard USB-C in ROM download mode. The console needs a 3.3 V USB-to-UART adapter on header H1: "
                "connect adapter TX to pin 13 (RX0), adapter RX to pin 15 (TX0), and connect ground.\n")
    dest.writestr("INSTALL.txt", f"""First installation for {args.board} only. Includes the bootloader and {example['id']} app.
This erases flash and replaces the partition table and application. Back up existing device data first.
{note}Register the board with EspHub and save its private NVS image as nvs.bin: download nvs.url with nvs.headers before nvs.expires_at and check nvs.sha256.
From this directory, use the correct local USB port:
esptool.py --chip {chip} --port PORT write_flash --erase-all @flash_args {nvs['offset']} nvs.bin
{example['description']}
The board starts in the bootloader and connects to EspHub. If the image has no Wi-Fi network, add one with the macOS BLE Wi-Fi tool or EH-WIFI commands on {console}.
Call bootloader_exit, type app on {console} or power-cycle to start the application.
""")

manifest = dict(board=args.board, chip=chip, version=version, **recipe, file=kit.name, nvs=nvs,
                sha256=hashlib.sha256(kit.read_bytes()).hexdigest(), example=example, app_sha256=hashlib.sha256(image).hexdigest(), app_size=len(image))
kit.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
