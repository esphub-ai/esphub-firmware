"""Package a shared EspHub bootloader for one board: the OTA image and the USB base image
that package-setup.py turns into the setup kit."""
import csv
import json
import os
import subprocess
import sys
from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZipFile

board, version = sys.argv[1:]
group, project = board.split("/")
profile = Path(__file__).resolve().parents[1] / "firmware/board" / group / project
metadata = json.loads((profile / "board.json").read_text())
shared = Path("shared-bl")
build = json.loads((shared / "build.json").read_text())
assert build["variant"] == metadata["bootloader"]
image = (shared / "esp-bootloader.bin").read_bytes()
assert sha256(image).hexdigest() == build["ota_sha256"]
assert image[288:].startswith(f"ESPHUB2/bootloader/{metadata['bootloader']}\0bootloader-ota-v2\0".encode())
chip = metadata["chip"]
prefix = f"esphub-{group}_{project}-{chip}-{version}"
out = Path("dist")
out.mkdir(exist_ok=True)
ota = prefix + "-ota.bin"
(out / ota).write_bytes(image)
idf = Path(os.environ["IDF_PATH"])


def package(table):
    usb = prefix + "-usb.zip"
    with TemporaryDirectory() as directory:
        tmp = Path(directory)
        table_bin = tmp / "partition-table.bin"
        subprocess.run([sys.executable, idf / "components/partition_table/gen_esp32part.py",
                        "--offset", build["partition_offset"], "--flash-size",
                        f"{metadata['flash_mb']}MB", table, table_bin], check=True)
        with table.open() as f:
            parts = {row[0]: row for row in csv.reader(f, skipinitialspace=True)
                     if len(row) >= 5 and not row[0].startswith("#")}
        bl, standby, app, ota_data = (parts[key] for key in ("launcher_a", "launcher_b", "app", "otadata"))
        assert bl[1:3] == ["app", "ota_0"] and standby[1:3] == ["app", "ota_1"] and app[1:3] == ["app", "ota_2"]
        assert len(image) <= min(int(bl[4], 0), int(standby[4], 0))
        assert ota_data[1:3] == ["data", "ota"]
        offsets = {
            build["bootloader_offset"]: "bootloader.bin",
            build["partition_offset"]: table_bin.name,
            bl[3]: "esp-bootloader.bin",
            ota_data[3]: "ota-data-initial.bin",
        }
        with ZipFile(out / usb, "w", ZIP_DEFLATED) as z:
            z.write(shared / "bootloader.bin", "bootloader.bin")
            z.write(shared / "esp-bootloader.bin", "esp-bootloader.bin")
            z.write(table_bin, table_bin.name)
            z.write(table, "partitions.csv")
            z.writestr("ota-data-initial.bin", b"\xff" * int(ota_data[4], 0))
            z.writestr("flash_args", " ".join(build["write_flash_args"]) + "\n" +
                       "".join(f"{offset} {name}\n" for offset, name in offsets.items()))
    return {"file": usb, "sha256": sha256((out / usb).read_bytes()).hexdigest()}


table = Path(__file__).resolve().parents[1] / "firmware/partitions" / f"{metadata['flash_mb']}mb.csv"
# Read by the kit job and the publisher. Only the OTA image is published.
manifest = {"board": board, "chip": chip, "version": version, "bootloader_version": build["version"],
            "ota": {"file": ota, "sha256": build["ota_sha256"], "size": len(image)},
            "usb": package(table)}
(out / (prefix + ".json")).write_text(json.dumps(manifest, indent=2) + "\n")
