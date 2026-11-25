"""Assemble board-specific USB and release assets from a shared EspHub bootloader."""
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


def package(table, suffix):
    usb = prefix + suffix + ".zip"
    with TemporaryDirectory() as directory:
        tmp = Path(directory)
        table_bin = tmp / "partition-table.bin"
        subprocess.run([sys.executable, idf / "components/partition_table/gen_esp32part.py",
                        "--offset", build["partition_offset"], "--flash-size",
                        f"{metadata['flash_mb']}MB", table, table_bin], check=True)
        with table.open() as f:
            parts = {row[0]: row for row in csv.reader(f, skipinitialspace=True)
                     if len(row) >= 5 and not row[0].startswith("#")}
        bl, standby, app, nvs, ota_data = (parts[key] for key in ("launcher_a", "launcher_b", "app", "nvs", "otadata"))
        assert bl[1:3] == ["app", "ota_0"] and standby[1:3] == ["app", "ota_1"] and app[1:3] == ["app", "ota_2"]
        assert len(image) <= min(int(bl[4], 0), int(standby[4], 0))
        assert nvs[1:3] == ["data", "nvs"] and ota_data[1:3] == ["data", "ota"]
        nvs_csv = tmp / "board.csv"
        nvs_csv.write_text("key,type,encoding,value\nokl,namespace,,\nhold,data,u8,1\n")
        nvs_bin = tmp / "board-nvs.bin"
        subprocess.run([sys.executable,
                        idf / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py",
                        "generate", nvs_csv, nvs_bin, nvs[4]], check=True)
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
            z.write(nvs_bin, nvs_bin.name)
            z.writestr("ota-data-initial.bin", b"\xff" * int(ota_data[4], 0))
            z.writestr("flash_args", " ".join(build["write_flash_args"]) + "\n" +
                       "".join(f"{offset} {name}\n" for offset, name in offsets.items()))
            z.writestr("INSTALL.txt", f"""For {board} only. Use the setup kit for a first installation.
This archive preserves NVS and resets boot selection to the bootloader.
""")
    return {"file": usb, "sha256": sha256((out / usb).read_bytes()).hexdigest()}, offsets


table = profile / "layout/partitions.csv"
usb, offsets = package(table, "-usb")
manifest = {"source_commit": build["source_commit"], "board": board, "chip": chip,
            "version": version, "flash_mb": metadata["flash_mb"],
            "bootloader": build["variant"], "bootloader_version": build["version"],
            "partition_sha256": sha256(table.read_bytes()).hexdigest(), "offsets": offsets,
            "ota": {"file": ota, "sha256": build["ota_sha256"], "size": len(image)}, "usb": usb}
(out / (prefix + ".json")).write_text(json.dumps(manifest, indent=2) + "\n")
