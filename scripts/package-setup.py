"""Complete a fresh-device USB kit using the board's bootloader build and application image."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("board")
parser.add_argument("bootloader", type=Path)
parser.add_argument("runtime", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
group, project = args.board.split("/")
root = Path(__file__).resolve().parents[1]
profile = root / "firmware/board" / group / project
kind = os.environ["RUNTIME_KIND"]
assert kind in ("micropython", "native")
recipe = {"tag": os.environ["TAG"], "bootloader_version": os.environ["BOOTLOADER_VERSION"],
          "runtime_version": os.environ["RUNTIME_VERSION"], "bootloader_tag": os.environ["BOOTLOADER_TAG"],
          "runtime_tag": os.environ["RUNTIME_TAG"]}
metadata = json.loads((profile / "board.json").read_text())
chip = metadata["chip"]
version = os.environ["VERSION"]
prefix = f"esphub-{group}_{project}-{chip}-{version}-setup"
kit = args.output / (prefix + ".zip")
image = args.runtime.read_bytes()
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
    if kind == "micropython":
        assert image[288:].startswith(
            f"ESPHUB2/micropython/{args.board}\0bootloader-ota-v2\0".encode())
    assert bootloader[12:14] == image[12:14], "Mismatched ESP chips"
    for name in base.namelist():
        if name not in ("flash_args", "INSTALL.txt"):
            dest.writestr(name, base.read(name))
    app_file = f"{kind}.bin"
    dest.writestr(app_file, image)
    if kind == "micropython":
        dest.write(profile / "main.py", "main.py")
    dest.write(root / "tools/ble-wifi/provision_usb.py", "provision_usb.py")
    flash_args = base.read("flash_args").rstrip() + f"\n{offset:#x} {app_file}\n".encode()
    name = "board-nvs.bin"
    assert len(base.read(name)) == int(parts["nvs"][4], 0)
    flash_args += f"{parts['nvs'][3]} {name}\n".encode()
    dest.writestr("flash_args", flash_args)
    setup_note = ("With the board in the bootloader, run python3 provision_usb.py PORT with the private devices.create JSON on stdin. Install pyserial and sign in to the Okou CLI with EspHub MCP write access. The helper writes the registration at 115200 baud and binds the returned Bluetooth address to its device ID.\n"
                  "Run the macOS BLE Wi-Fi tool while the board is in the bootloader. Call bootloader.exit, type app on the provisioning port or power-cycle to start the application.")
    if args.board == "espressif/esp-mosaico":
        setup_note = ("Flash through onboard USB-C in ROM download mode. For provision_usb.py, select the port of a 3.3 V USB-to-UART adapter on header H1. "
                      "Connect adapter TX to pin 13 (RX0), adapter RX to pin 15 (TX0), and connect ground.\n" + setup_note)
    app_label = "MicroPython" if kind == "micropython" else "the ESP-Mosaico native Hello World"
    verification = ("The included main.py is the board verification example. Install it after setup using Python OTA."
                    if kind == "micropython" else "Check the display for Espressif’s Hello World and verify the native app in devices.get.")
    dest.writestr("INSTALL.txt", f"""First installation for {args.board} only. Includes the bootloader and {app_label}.
This erases flash and replaces the partition table and application. Back up existing device data first.
From this directory, use the correct local USB port:
esptool.py --chip {chip} --port PORT write_flash --erase-all @flash_args
{verification}
{setup_note}
""")

manifest = dict(board=args.board, chip=chip, version=version, **recipe, file=kit.name,
                download_url=f"https://api.esphub.ai/releases/{args.board}/setup?tag={recipe['tag']}",
                sha256=hashlib.sha256(kit.read_bytes()).hexdigest(),
                usb_wifi_setup=b"esphub-usb-wifi-v1\0" in bootloader,
                runtime_kind=kind, runtime_sha256=hashlib.sha256(image).hexdigest(), runtime_size=len(image))
kit.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
