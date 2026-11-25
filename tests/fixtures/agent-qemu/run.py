#!/usr/bin/env python3
"""Build the real Agent SDK probe out of tree and require real QEMU UART PASS."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

PASS = "ESPHUB-AGENT-QEMU PASS ALL"
FAIL = ("ESPHUB-AGENT-QEMU FAIL", "Guru Meditation Error", "abort() was called", "stack overflow")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-xtensa")
    parser.add_argument("--target", choices=("esp32", "esp32s3"), default="esp32s3")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--seconds", type=float, default=20)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    build = (args.build_dir or Path(tempfile.mkdtemp(prefix="esphub-agent-qemu-"))).resolve()
    repository = source.parents[2]
    if build == repository or repository in build.parents:
        parser.error("--build-dir must be outside the repository")
    build.mkdir(parents=True, exist_ok=True)
    idf = os.environ.get("IDF_PATH")
    if not idf:
        parser.error("set IDF_PATH and activate the ESP-IDF v5.4.1 Python/compiler environment")
    # Only direct SDK dependencies on IDF 5.4.1; never generate a source-tree lock file.
    env = {**os.environ, "IDF_COMPONENT_MANAGER": "0"}
    cmd = [sys.executable, str(Path(idf) / "tools/idf.py"), "-C", str(source),
           "-B", str(build), "-D", f"SDKCONFIG={build / 'sdkconfig'}",
           "-D", f"IDF_TARGET={args.target}", "build"]
    print("Build directory:", build, flush=True)
    with (build / "build.log").open("w") as log:
        subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    flash = build / "flash.bin"
    with (build / "merge.log").open("w") as log:
        subprocess.run([sys.executable, "-m", "esptool", "--chip", args.target,
                        "merge_bin", "-o", str(flash), "--fill-flash-size", "4MB", "@flash_args"],
                       cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    uart = build / "uart.log"
    uart.write_text("")
    cmd = [args.qemu, "-M", args.target, "-display", "none", "-monitor", "none",
           "-serial", f"file:{uart}", "-no-reboot", "-snapshot", "-nic", "none",
           "-drive", f"file={flash},if=mtd,format=raw"]
    (build / "qemu-command.json").write_text(json.dumps(cmd, indent=2) + "\n")
    with (build / "qemu.log").open("w") as log:
        proc = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL,
                                stdout=log, stderr=subprocess.STDOUT)
        end = time.monotonic() + args.seconds
        passed = None
        try:
            while proc.poll() is None and time.monotonic() < end:
                text = uart.read_text(errors="replace")
                if any(marker in text for marker in FAIL):
                    break
                if PASS in text and passed is None:
                    passed = time.monotonic()
                if passed is not None and time.monotonic() - passed >= 0.2:
                    break
                time.sleep(0.02)
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    text = uart.read_text(errors="replace")
    print(text, end="")
    print("UART log:", uart)
    return 0 if passed is not None and PASS in text and not any(marker in text for marker in FAIL) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Build/merge failed; inspect build.log/merge.log in the printed build directory ({exc}).", file=sys.stderr)
        sys.exit(1)
