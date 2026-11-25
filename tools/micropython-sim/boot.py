"""Observe the CI kit's main.py when the ESPHub runtime starts it normally."""

import esp32
import gc
import io
import machine
import okou
import os
import time as _qa_time

with open("case.txt") as f:
    _qa_board, _qa_chip, _qa_size = f.read().split()
_qa_size = int(_qa_size)
_qa_uart = 0x3FF40000 if _qa_chip == "esp32" else 0x60000000


def serial(line):
    for byte in (line + "\r\n").encode():
        machine.mem32[_qa_uart] = byte


partition = esp32.Partition.find(esp32.Partition.TYPE_DATA, label="data")[0]
assert partition.info()[3] == _qa_size
assert gc.mem_free() > 0
_qa_model_name = os.uname().machine
serial("ESPHUB-QA BOOT " + _qa_board)
serial("ESPHUB-QA MACHINE " + _qa_model_name)


def _qa_panel_done(evidence):
    serial("ESPHUB-QA PANEL " + evidence)
    serial("ESPHUB-QA AUTOMATIC main.py")
    serial("ESPHUB-QA PASS " + _qa_board)
    while True:
        _qa_time.sleep_ms(50)


_fixtures = None
with open("mode.txt") as f:
    _qa_mode = f.read()
if _qa_mode in ("model", "forward"):
    import M5 as _native_m5

    assert callable(_native_m5.begin)
    if _qa_board in ("m5stack/sticks3", "m5stack/atoms3r", "m5stack/papers3", "m5stack/cores3"):
        filename = "input-model.py"
    else:
        filename = "panel-model.py"
    _fixtures = {
        "board": _qa_board,
        "fixture_board": _qa_board,
        "qa_finish": _qa_panel_done,
    }
    if _qa_mode == "forward":
        _fixtures["native_m5"] = _native_m5
    with open(filename) as f:
        exec(f.read(), _fixtures)


class Console(io.IOBase):
    def __init__(self):
        self.line = bytearray()
        self.lines = 0

    def readinto(self, buf):
        return None

    def ioctl(self, op, arg):
        return 0 if op == 3 else -1

    def write(self, buf):
        for byte in buf:
            if byte == 10:
                if self.line:
                    line = self.line.decode()
                    self.line = bytearray()
                    serial("QA observed: " + line)
                    try:
                        self.observe(line)
                    except Exception as exc:
                        serial("ESPHUB-QA FAIL " + _qa_board + " " + repr(exc))
            elif byte != 13:
                self.line.append(byte)
        return len(buf)

    def observe(self, line):
        if _fixtures is not None:
            if "lines" in _fixtures:
                _fixtures["lines"].append(line)
            return
        if self.lines >= 4:
            return
        if _qa_board in (
            "m5stack/stopwatch",
            "m5stack/paper-color",
            "m5stack/cardputer-adv",
            "m5stack/paper-diy",
            "m5stack/stackchan",
        ):
            if line.startswith("ESP Hub hello drawn: " + _qa_board):
                self.lines = 4
        elif self.lines == 0:
            assert line == "Board: " + _qa_model_name
            self.lines += 1
        elif self.lines == 1:
            assert line == "Data partition: %d bytes" % _qa_size
            self.lines += 1
        elif self.lines == 2:
            assert line.startswith("Free heap: ") and line.endswith(" bytes")
            assert int(line.split()[2]) > 0
            self.lines += 1
        else:
            rssi = okou.wifi_rssi()
            assert line == (
                "Wi-Fi RSSI: offline" if rssi is None else "Wi-Fi RSSI: %d dBm" % rssi
            )
            self.lines += 1
        if self.lines == 4:
            serial("ESPHUB-QA AUTOMATIC main.py")
            serial("ESPHUB-QA PASS " + _qa_board)


# _boot.py has already mounted LittleFS. Hand console output to the test serial
# and return, allowing the normal C entry point to load the unmodified main.py.
os.dupterm(Console(), 0)
