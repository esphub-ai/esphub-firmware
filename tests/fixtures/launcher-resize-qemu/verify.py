#!/usr/bin/env python3
"""Cold-boot the resize probe three times against the same disposable QEMU flash."""
import argparse
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import time


def partitions(contents):
    rows = {}
    for offset in range(0, 0xc00, 32):
        magic, _, _, start, size, name, _ = struct.unpack_from('<HBBII16sI', contents, offset)
        if magic != 0x50aa:
            break
        rows[name.rstrip(b'\0').decode()] = (start, size)
    return rows


def run(build, qemu, work):
    args = json.loads((build / 'flasher_args.json').read_text())
    table_address = int(args['partition-table']['offset'], 0)
    baseline = (build / args['partition-table']['file']).read_bytes()
    fixed = partitions(baseline)
    app_start = fixed['app'][0]
    app_file = args['app']['file']
    image = (build / app_file).read_bytes()
    flash = work / 'flash.bin'
    subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'merge_bin',
                    '-o', str(flash), '--fill-flash-size', args['flash_settings']['flash_size'],
                    '@flash_args', hex(app_start), app_file], cwd=build, check=True)
    original = flash.read_bytes()
    for phase in range(3):
        uart = work / f'uart-{phase}.log'
        uart.touch()
        with (work / f'qemu-{phase}.log').open('w+') as stream, subprocess.Popen([
            qemu, '-M', 'esp32s3', '-display', 'none', '-monitor', 'none', '-serial', f'file:{uart}',
            '-no-reboot', '-nic', 'none', '-drive', f'file={flash},if=mtd,format=raw,cache=writethrough',
        ], stdin=subprocess.DEVNULL, stdout=stream, stderr=stream) as proc:
            try:
                deadline = time.monotonic() + 30
                mark = f'RESIZE PROBE PASS {phase}'
                while time.monotonic() < deadline:
                    output = uart.read_text(errors='replace')
                    if mark in output:
                        break
                    if proc.poll() is not None or any(s in output for s in ('assert failed', 'Guru Meditation', 'abort()', 'ESP_ERROR_CHECK failed')):
                        raise RuntimeError(output + (work / f'qemu-{phase}.log').read_text())
                    time.sleep(0.03)
                else:
                    raise RuntimeError(output + (work / f'qemu-{phase}.log').read_text())
            finally:
                proc.terminate()
                proc.wait(timeout=5)
        after = flash.read_bytes()
        active = partitions(after[table_address:table_address + 0xc00])
        assert active['app'] == (app_start, (5 if phase == 0 else 4) * 1048576)
        assert active['data'][0] == sum(active['app']) and sum(active['data']) == len(after)
        assert {k: active[k] for k in fixed if k not in ('app', 'data')} == {
            k: v for k, v in fixed.items() if k not in ('app', 'data')}
        for name in ('launcher_a', 'launcher_b'):
            start, size = fixed[name]
            assert after[start:start + size] == original[start:start + size]
        assert after[app_start:app_start + len(image)] == image
        print(mark, active['app'], active['data'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='ESP-IDF build directory for this probe')
    parser.add_argument('--qemu', default='qemu-system-xtensa')
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='esphub-resize-') as directory:
        run(options.build.resolve(), options.qemu, Path(directory))
