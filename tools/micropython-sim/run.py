"""Boot an original ESPHub USB kit and check its default main.py through UART."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
from zipfile import ZipFile
import zlib

from littlefs import LittleFS

FIXTURES = Path(__file__).resolve().parent
PANEL_CASES = {
    "m5stack/sticks3",
    "m5stack/atoms3r",
    "m5stack/papers3",
    "m5stack/cores3",
    "m5stack/cardputer-adv",
    "m5stack/stackchan",
    "m5stack/paper-diy",
}
INTERACTIVE_CASES = {"m5stack/sticks3", "m5stack/atoms3r", "m5stack/papers3", "m5stack/cores3"}
PANICS = (
    "Guru Meditation Error",
    "abort() was called",
    "stack overflow",
    "ESPHUB-QA FAIL",
)


def make_flash(kit, meta, image_path, panel_mode):
    """Use the kit's layout and binaries. Only the temporary OTA state and data FS are seeded."""
    with ZipFile(kit) as archive:
        rows = csv.reader(
            archive.read("partitions.csv").decode().splitlines(), skipinitialspace=True
        )
        layout = {row[0]: row for row in rows if row and not row[0].startswith("#")}
        data_offset, data_size = (int(value, 0) for value in layout["data"][3:5])
        flash = bytearray(b"\xff") * (data_offset + data_size)
        for line in archive.read("flash_args").decode().splitlines():
            if line.startswith("0x"):
                address, name = line.split()
                offset = int(address, 0)
                part = archive.read(name)
                assert offset + len(part) <= len(flash)
                flash[offset : offset + len(part)] = part

        # esp_ota_select_entry_t: sequence for app's ota_N slot, VALID state and IDF CRC.
        slot = int(layout["app"][2].split("_")[1])
        sequence = struct.pack("<I", slot + 1)
        entry = (
            sequence
            + b"\xff" * 20
            + struct.pack("<II", 2, zlib.crc32(sequence, 0xFFFFFFFF))
        )
        start = int(layout["otadata"][3], 0)
        flash[start : start + len(entry)] = entry

        files = {
            "main.py": archive.read("main.py"),
            "case.txt": f'{meta["board"]} {meta["chip"]} {data_size}'.encode(),
            "mode.txt": panel_mode.encode(),
            "boot.py": (FIXTURES / "boot.py").read_bytes(),
        }
        if panel_mode in {"model", "forward"}:
            fixture = (
                "input-model.py"
                if meta["board"] in INTERACTIVE_CASES
                else "panel-model.py"
            )
            files[fixture] = (FIXTURES / fixture).read_bytes()
        fs = LittleFS(
            block_size=4096, block_count=data_size // 4096, prog_size=32, read_size=32
        )
        for name, content in files.items():
            with fs.open(name, "wb") as output:
                output.write(content)
        fs.unmount()
        flash[data_offset : data_offset + data_size] = fs.context.buffer
        image_path.write_bytes(flash)


def run(meta, kit, qemu_src, panel_mode, seconds):
    board, chip = meta["board"], meta["chip"]
    xtensa = chip in {"esp32", "esp32s3"}
    executable = qemu_src / (
        "build/qemu-system-xtensa" if xtensa else "build-rv/qemu-system-riscv32"
    )
    marker = f"ESPHUB-QA PASS {board}"
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        image, uart = root / "flash.bin", root / "uart.log"
        make_flash(kit, meta, image, panel_mode)
        cmd = [
            str(executable),
            "-L",
            str(qemu_src / "pc-bios"),
            "-M",
            chip,
            "-display",
            "none",
            "-monitor",
            "none",
            "-snapshot",
            "-no-reboot",
            "-drive",
            f"file={image},if=mtd,format=raw",
            "-serial",
            f"file:{uart}",
        ]
        if xtensa:
            cmd += ["-m", "8M" if chip == "esp32s3" else "4M"]
        if board in {"m5stack/papermono", "m5stack/stopwatch", "m5stack/stackchan"}:
            cmd += ["-global", "ssi_psram.is_octal=true"]
        if board == "m5stack/cardputer-adv":
            # ESP32-S3 ADC calibration revision 1, eFuse block 2 bit 128.
            efuse = root / "efuse.bin"
            simulated = bytearray(1024)
            simulated[64] = 1
            efuse.write_bytes(simulated)
            cmd += [
                "-drive",
                f"file={efuse},if=none,format=raw,id=qaefuse",
                "-global",
                "driver=nvram.esp32s3.efuse,property=drive,value=qaefuse",
            ]
        env = os.environ.copy()
        if board == "m5stack/paper-v11":
            env["ESPHUB_QA_PAPER_HRDY"] = "1"

        with (root / "qemu.log").open("w") as debug:
            proc = subprocess.Popen(cmd, stdout=debug, stderr=debug, env=env)
            deadline = time.monotonic() + seconds
            try:
                while proc.poll() is None and time.monotonic() < deadline:
                    output = uart.read_text(errors="replace") if uart.exists() else ""
                    if any(word in output for word in PANICS):
                        break
                    if marker in output.splitlines():
                        time.sleep(2)
                        break
                    time.sleep(0.2)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                proc.wait()

        output = uart.read_text(errors="replace") if uart.exists() else ""
        print(output)
        print((root / "qemu.log").read_text(errors="replace"))
        return marker in output.splitlines() and not any(
            word in output for word in PANICS
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "manifest", type=Path, help="CI or release setup JSON beside its setup ZIP"
    )
    parser.add_argument("--qemu-source", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=75)
    parser.add_argument(
        "--native-panel",
        action="store_true",
        help="Probe native graphics on display examples that normally use a modeled panel",
    )
    args = parser.parse_args()
    meta = json.loads(args.manifest.read_text())
    kit = args.manifest.parent / meta["file"]
    if hashlib.sha256(kit.read_bytes()).hexdigest() != meta["sha256"]:
        parser.error("Setup ZIP SHA-256 differs from the manifest")
    panel_mode = "model" if meta["board"] in PANEL_CASES else "native"
    if args.native_panel:
        panel_mode = "forward" if meta["board"] in INTERACTIVE_CASES else "native"
    return (
        0 if run(meta, kit, args.qemu_source.resolve(), panel_mode, args.seconds) else 1
    )


if __name__ == "__main__":
    sys.exit(main())
